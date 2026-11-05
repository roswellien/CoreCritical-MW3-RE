#pragma once

#include <algorithm>
#include <cfenv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define CC_RENDER_WORKER_SSE 1
#endif

namespace cc_mw3_re {
// A single ordered submission owner. Callers transfer owned packets to post();
// call()/drain() are explicit barriers. Failure permanently closes the queue.
class RenderWorker {
  public:
    struct Statistics {
        std::size_t submitted{}, completed{}, peak_jobs{}, peak_bytes{}, waits{};
    };
    explicit RenderWorker(std::size_t maximum_jobs = 8,
                          std::size_t maximum_bytes = 128 * 1024 * 1024,
                          std::function<void()> wait_pump = {})
        : maximum_jobs_(maximum_jobs), maximum_bytes_(maximum_bytes),
          wait_pump_(std::move(wait_pump)) {
        if (!maximum_jobs_ || !maximum_bytes_)
            throw std::invalid_argument("Render worker limits must be positive");
        thread_ = std::thread([this] { run(); });
    }
    ~RenderWorker() { stop(); }
    RenderWorker(const RenderWorker &) = delete;
    RenderWorker &operator=(const RenderWorker &) = delete;

    void post(std::function<void()> function, std::size_t bytes = 0) {
        if (std::this_thread::get_id() == thread_.get_id())
            throw std::logic_error("Render worker cannot enqueue onto itself");
        Job job{std::move(function), bytes, {}};
#ifdef CC_RENDER_WORKER_SSE
        job.mxcsr = _mm_getcsr();
#endif
        if (std::fegetenv(&job.environment) != 0)
            throw std::runtime_error("Cannot snapshot render packet FP state");
        std::unique_lock<std::mutex> lock(mutex_);
        const auto room = [&] {
            // One oversized packet is allowed only with no outstanding work.
            return outstanding_ == 0 || (outstanding_ < maximum_jobs_ && bytes <= maximum_bytes_ &&
                                         outstanding_bytes_ <= maximum_bytes_ - bytes);
        };
        if (!room() && !closed_ && !failure_)
            ++statistics_.waits;
        wait(lock, [&] { return closed_ || failure_ || room(); });
        check_failure();
        if (closed_)
            throw std::runtime_error("Render worker is closed");
        jobs_.push_back(std::move(job));
        ++outstanding_;
        outstanding_bytes_ += bytes;
        ++statistics_.submitted;
        statistics_.peak_jobs = std::max(statistics_.peak_jobs, outstanding_);
        statistics_.peak_bytes = std::max(statistics_.peak_bytes, outstanding_bytes_);
        changed_.notify_all();
    }

    template <class Function> auto call(Function function) -> decltype(function()) {
        using Result = decltype(function());
        auto completion = std::make_shared<std::promise<Result>>();
        auto future = completion->get_future();
        auto owned = std::make_shared<Function>(std::move(function));
        post([completion, owned] {
            try {
                if constexpr (std::is_void<Result>::value) {
                    (*owned)();
                    completion->set_value();
                } else {
                    completion->set_value((*owned)());
                }
            } catch (...) {
                completion->set_exception(std::current_exception());
                throw;
            }
        });
        // A failed earlier job can cancel this callback. Waiting on the promise
        // alone would deadlock while this caller still owns its promise.
        drain();
        return future.get();
    }
    void drain() {
        if (std::this_thread::get_id() == thread_.get_id())
            throw std::logic_error("Render worker cannot wait on itself");
        std::unique_lock<std::mutex> lock(mutex_);
        wait(lock, [&] { return failure_ || outstanding_ == 0; });
        check_failure();
    }
    Statistics statistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return statistics_;
    }
    std::thread::id thread_id() const { return thread_.get_id(); }
    void stop() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            changed_.notify_all();
        }
        if (thread_.joinable())
            thread_.join();
    }

  private:
    template <class Predicate> void wait(std::unique_lock<std::mutex> &lock, Predicate ready) {
        if (!wait_pump_) {
            changed_.wait(lock, ready);
            return;
        }
        while (!ready()) {
            changed_.wait_for(lock, std::chrono::milliseconds(1));
            lock.unlock();
            wait_pump_();
            lock.lock();
        }
    }
    struct Job {
        std::function<void()> function;
        std::size_t bytes;
        std::fenv_t environment;
#ifdef CC_RENDER_WORKER_SSE
        unsigned mxcsr{};
#endif
    };
    void check_failure() const {
        if (failure_)
            std::rethrow_exception(failure_);
    }
    void run() noexcept {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [&] { return closed_ || !jobs_.empty(); });
                if (jobs_.empty())
                    return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            std::exception_ptr error;
            std::fenv_t saved{};
            const bool saved_ok = std::fegetenv(&saved) == 0;
#ifdef CC_RENDER_WORKER_SSE
            const auto saved_mxcsr = _mm_getcsr();
#endif
            try {
                if (!saved_ok || std::fesetenv(&job.environment) != 0)
                    throw std::runtime_error("Cannot establish render worker FP state");
#ifdef CC_RENDER_WORKER_SSE
                _mm_setcsr(job.mxcsr);
#endif
                job.function();
            } catch (...) {
                error = std::current_exception();
            }
            if (saved_ok && std::fesetenv(&saved) != 0 && !error)
                error =
                    std::make_exception_ptr(std::runtime_error("Cannot restore worker FP state"));
#ifdef CC_RENDER_WORKER_SSE
            _mm_setcsr(saved_mxcsr);
#endif
            job.function = {};
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (error) {
                    failure_ = error;
                    closed_ = true;
                    jobs_.clear();
                    outstanding_ = outstanding_bytes_ = 0;
                } else {
                    --outstanding_;
                    outstanding_bytes_ -= job.bytes;
                    ++statistics_.completed;
                }
                changed_.notify_all();
            }
            if (error)
                return;
        }
    }
    const std::size_t maximum_jobs_, maximum_bytes_;
    const std::function<void()> wait_pump_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Job> jobs_;
    std::size_t outstanding_{}, outstanding_bytes_{};
    bool closed_{};
    std::exception_ptr failure_;
    Statistics statistics_;
    std::thread thread_;
};
} // namespace cc_mw3_re
#undef CC_RENDER_WORKER_SSE
