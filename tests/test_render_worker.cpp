#include "frontend/core_critical/render_worker.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using cc_mw3_re::RenderWorker;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void ordered_ownership() {
    RenderWorker worker;
    const auto caller = std::this_thread::get_id();
    require(worker.call([] { return std::this_thread::get_id(); }) != caller,
            "Submission still executes on the producer");
    std::vector<int> order;
    auto packet = std::make_shared<std::vector<int>>(std::initializer_list<int>{7});
    std::weak_ptr<std::vector<int>> lifetime = packet;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    worker.post([packet, gate, &entered, &order] {
        entered.set_value();
        gate.wait();
        order.push_back(packet->at(0));
    });
    entered.get_future().get();
    packet.reset();
    require(!lifetime.expired(), "Queued packet lost its resource ownership");
    worker.post([&] { order.push_back(8); });
    release.set_value();
    const auto size = worker.call([&] { return order.size(); });
    require(size == 2 && order == std::vector<int>{7, 8}, "Submission order or barrier changed");
    require(lifetime.expired(), "Completed packet retains its resource ownership");
    bool rejected = false;
    worker.call([&] {
        try {
            worker.drain();
        } catch (const std::logic_error &) {
            rejected = true;
        }
    });
    require(rejected, "Worker can deadlock waiting on itself");
    worker.stop();
    try {
        worker.post([] {});
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("Closed worker accepted new work");
}

void bounded_queue(std::size_t maximum_jobs, std::size_t maximum_bytes) {
    RenderWorker worker(maximum_jobs, maximum_bytes);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    worker.post(
        [&] {
            entered.set_value();
            gate.wait();
        },
        7);
    entered.get_future().get();
    std::promise<void> submitting;
    auto producer = std::async(std::launch::async, [&] {
        submitting.set_value();
        worker.post([] {}, 4);
    });
    submitting.get_future().get();
    // Wait for actual backpressure, independent of thread scheduling speed.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!worker.statistics().waits && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool blocked =
        worker.statistics().waits == 1 &&
        producer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
    release.set_value();
    producer.get();
    worker.drain();
    require(blocked && worker.statistics().peak_bytes <= maximum_bytes &&
                worker.statistics().peak_jobs <= maximum_jobs,
            "Packet job/byte budget is not enforced");
    worker.post([] {}, maximum_bytes * 2); // An oversize packet must not deadlock admission.
    worker.drain();
    require(worker.statistics().peak_bytes == maximum_bytes * 2 &&
                worker.statistics().peak_jobs == 1,
            "Oversize packet was not admitted exclusively");
}

void floating_point() {
    RenderWorker worker;
    std::fenv_t saved{};
    std::fegetenv(&saved);
    std::fesetround(FE_DOWNWARD);
    const int rounding = worker.call([] { return std::fegetround(); });
    std::fesetenv(&saved);
    require(rounding == FE_DOWNWARD, "Packet did not snapshot producer FP rounding");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    const auto original = _mm_getcsr();
    _mm_setcsr(original ^ (1U << 15));
    const auto expected = _mm_getcsr();
    const auto observed = worker.call([] { return _mm_getcsr(); });
    _mm_setcsr(original);
    require((observed & (1U << 15)) == (expected & (1U << 15)),
            "Packet did not snapshot producer flush-to-zero state");
#endif
}

void failure_cancels_barrier() {
    RenderWorker worker;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    worker.post([&] {
        entered.set_value();
        gate.wait();
        throw std::runtime_error("intentional worker failure");
    });
    entered.get_future().get();
    std::atomic<bool> executed{false};
    auto barrier = std::async(std::launch::async, [&] {
        try {
            worker.call([&] { executed = true; });
        } catch (const std::runtime_error &error) {
            return std::string(error.what());
        }
        return std::string{};
    });
    while (worker.statistics().submitted != 2)
        std::this_thread::yield();
    release.set_value();
    require(barrier.get() == "intentional worker failure" && !executed,
            "Failure did not cancel and wake a pending barrier");
    try {
        worker.drain();
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()) == "intentional worker failure", "Failure identity lost");
        return;
    }
    throw std::runtime_error("Failure silently recovered");
}
} // namespace

int main() {
    try {
        ordered_ownership();
        bounded_queue(2, 10);  // Byte budget binds first.
        bounded_queue(1, 100); // Job limit binds even when bytes would fit.
        floating_point();
        failure_cancels_barrier();
        std::puts("Render worker ownership, bounds, FP and failure checks passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
