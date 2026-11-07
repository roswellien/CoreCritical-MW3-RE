#pragma once

namespace ztwod {
// Native updates still run under this scope. Only presentation output and
// widget hit testing are masked; timed/queued draws and RNG retain their order.
inline thread_local bool presentation_output = true;
inline bool presentation_output_enabled() { return presentation_output; }
class PresentationScope {
  public:
    explicit PresentationScope(bool visible) : previous_(presentation_output) {
        presentation_output = previous_ && visible;
    }
    ~PresentationScope() { presentation_output = previous_; }
    PresentationScope(const PresentationScope &) = delete;
    PresentationScope &operator=(const PresentationScope &) = delete;

  private:
    bool previous_;
};
} // namespace ztwod
