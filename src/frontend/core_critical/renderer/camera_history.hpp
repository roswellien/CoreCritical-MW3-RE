#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>

namespace mw34::temporal {
inline bool enabled() {
    static const bool value = [] {
        const char *setting = std::getenv("MW34_TEMPORAL_INPUTS");
        if (!setting || !*setting || !std::strcmp(setting, "0"))
            return false;
        if (!std::strcmp(setting, "1"))
            return true;
        throw std::runtime_error("MW34_TEMPORAL_INPUTS must be 0 or 1");
    }();
    return value;
}
enum class Reset {
    first,
    none,
    missing,
    changed,
    ambiguous,
    external_write,
    transition,
    non_flip,
    wrong_target,
    invalid,
    capacity,
    sequence
};
inline const char *name(Reset reason) {
    switch (reason) {
    case Reset::first:
        return "first";
    case Reset::none:
        return "none";
    case Reset::missing:
        return "missing";
    case Reset::changed:
        return "changed";
    case Reset::ambiguous:
        return "ambiguous";
    case Reset::external_write:
        return "guest-write";
    case Reset::transition:
        return "transition";
    case Reset::non_flip:
        return "non-flip";
    case Reset::wrong_target:
        return "wrong-target";
    case Reset::invalid:
        return "invalid";
    case Reset::capacity:
        return "capacity";
    case Reset::sequence:
        return "sequence";
    }
    throw std::runtime_error("Unknown camera history reset");
}
struct View {
    std::uint32_t context{}, node{}, data{}, role{};
    auto operator<=>(const View &) const = default;
};
// Values only: no retained guest-memory reads or callbacks. Projection is in
// guest screen coordinates after host aspect/zoom adaptation, before jitter.
struct Snapshot {
    View view;
    std::uint64_t frame{}, load_epoch{}, reset_epoch{}, camera_epoch{};
    std::uint32_t target{}, attachment{}, viewport{}, controller{}, mode{};
    std::array<std::uint32_t, 12> camera{}, inverse{};
    std::array<float, 6> projection{};
    float far_clip{};
    std::array<unsigned, 4> viewport_rect{};
    std::array<unsigned, 6> sizes{}; // guest, output, configured internal world
    bool operator==(const Snapshot &) const = default;
    bool finite(bool require_sizes = false) const {
        if (!view.context || !view.node || !view.data || !target || !attachment || !viewport)
            return false;
        for (auto word : camera)
            if (!std::isfinite(std::bit_cast<float>(word)))
                return false;
        for (auto word : inverse)
            if (!std::isfinite(std::bit_cast<float>(word)))
                return false;
        for (float value : projection)
            if (!std::isfinite(value))
                return false;
        if (!std::isfinite(far_clip) || far_clip <= projection[5])
            return false;
        if (require_sizes)
            for (unsigned size : sizes)
                if (!size)
                    return false;
        return projection[0] > 0 && projection[1] != 0 && projection[2] != 0 && projection[5] > 0 &&
               viewport_rect[2] && viewport_rect[3];
    }
    bool compatible(const Snapshot &previous) const {
        // Camera pose is deliberately absent: continuous movement needs the
        // prior pose. Identity, reset provenance and effective projection agree.
        return view == previous.view && load_epoch == previous.load_epoch &&
               reset_epoch == previous.reset_epoch && camera_epoch == previous.camera_epoch &&
               target == previous.target && attachment == previous.attachment &&
               viewport == previous.viewport && controller == previous.controller &&
               mode == previous.mode && projection == previous.projection &&
               viewport_rect == previous.viewport_rect && far_clip == previous.far_clip &&
               sizes == previous.sizes;
    }
};
struct Pair {
    std::shared_ptr<const Snapshot> current, previous;
    Reset reset{Reset::first};
    bool valid() const { return current && previous && reset == Reset::none; }
};
class History {
    std::map<View, std::shared_ptr<const Snapshot>> pending_, completed_;
    std::map<View, Pair> pairs_;
    std::uint64_t last_frame_{};
    Reset pending_reset_{Reset::none}, next_reset_{Reset::first}, last_reset_{Reset::first};
    const char *disagreement_{"none"};

  public:
    static constexpr unsigned capacity = 8;
    const auto &pairs() const { return pairs_; }
    std::uint64_t completed_frame() const { return last_frame_; }
    bool pending() const { return !pending_.empty(); }
    Reset last_reset() const { return last_reset_; }
    const char *disagreement() const { return disagreement_; }
    void invalidate(Reset reason) {
        completed_.clear();
        pairs_.clear();
        // Keep this frame poisoned even if a later submission has valid data.
        pending_.clear();
        pending_reset_ = reason;
        next_reset_ = reason;
    }
    void observe(std::shared_ptr<const Snapshot> snapshot) {
        if (pending_reset_ != Reset::none)
            return;
        if (pending_.empty())
            disagreement_ = "none";
        if (!snapshot || !snapshot->finite(true)) {
            invalidate(Reset::invalid);
            return;
        }
        if (snapshot->frame != last_frame_ + 1) {
            invalidate(Reset::sequence);
            return;
        }
        const auto existing = pending_.find(snapshot->view);
        if (existing != pending_.end()) {
            if (*existing->second != *snapshot) {
                const auto &before = *existing->second;
                disagreement_ = before.camera != snapshot->camera               ? "pose"
                                : before.inverse != snapshot->inverse           ? "inverse"
                                : before.projection != snapshot->projection     ? "projection"
                                : before.camera_epoch != snapshot->camera_epoch ? "camera-epoch"
                                : before.reset_epoch != snapshot->reset_epoch   ? "reset-epoch"
                                : before.sizes != snapshot->sizes               ? "sizes"
                                                                                : "identity";
                invalidate(Reset::ambiguous);
            }
            return;
        }
        if (pending_.size() == capacity) {
            invalidate(Reset::capacity);
            return;
        }
        pending_.emplace(snapshot->view, std::move(snapshot));
    }
    void complete(std::uint64_t frame, std::uint32_t flipped_target) {
        // Reject a duplicate without replacing an already completed pair.
        if (frame <= last_frame_)
            throw std::runtime_error("Camera history completion is duplicate or out of order");
        if (frame != last_frame_ + 1)
            invalidate(Reset::sequence);
        last_frame_ = frame;
        pairs_.clear();
        if (!flipped_target)
            invalidate(Reset::non_flip);
        bool wrong_target = false;
        for (const auto &[view, snapshot] : pending_)
            wrong_target |= snapshot->target != flipped_target;
        if (wrong_target)
            invalidate(Reset::wrong_target);
        if (pending_reset_ != Reset::none || pending_.empty()) {
            completed_.clear();
            next_reset_ = pending_reset_ == Reset::none ? Reset::missing : pending_reset_;
            last_reset_ = next_reset_;
        } else {
            for (const auto &[view, snapshot] : pending_) {
                Pair pair{snapshot, {}, next_reset_};
                const auto previous = completed_.find(view);
                if (previous != completed_.end()) {
                    if (previous->second->frame + 1 == frame &&
                        snapshot->compatible(*previous->second)) {
                        pair.previous = previous->second;
                        pair.reset = Reset::none;
                    } else
                        pair.reset = Reset::changed;
                } else if (!completed_.empty())
                    pair.reset = Reset::missing;
                pairs_.emplace(view, std::move(pair));
            }
            completed_ = std::move(pending_);
            next_reset_ = Reset::first;
            last_reset_ = Reset::none;
            for (const auto &[view, pair] : pairs_)
                if (!pair.valid()) {
                    last_reset_ = pair.reset;
                    break;
                }
        }
        pending_.clear();
        pending_reset_ = Reset::none;
    }
};
} // namespace mw34::temporal
