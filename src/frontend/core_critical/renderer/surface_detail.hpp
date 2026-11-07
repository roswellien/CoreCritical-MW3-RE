#pragma once
#include "sun_lighting.hpp"
#include <chrono>
#include <cstdint>

namespace mw34::detail {
// Presentation-only state. No guest clock queries or simulation writes.
struct Settings {
    bool water{}, clouds{};
    float water_strength{.45f}, cloud_strength{.35f}, scale{.06f}, speed{.025f};
};
// Read and validate the environment once on first use. Later environment changes
// do not update an already running presentation profile.
inline const Settings &settings() {
    static const Settings value = [] {
        Settings detail_settings;
        detail_settings.water = sun::flag("MW34_WATER_DETAIL");
        detail_settings.clouds = sun::flag("MW34_CLOUD_DETAIL");
        detail_settings.water_strength = sun::parameter("MW34_WATER_DETAIL_STRENGTH", .45f, 0, 2);
        detail_settings.cloud_strength = sun::parameter("MW34_CLOUD_DETAIL_STRENGTH", .35f, 0, 2);
        detail_settings.scale = sun::parameter("MW34_WATER_DETAIL_SCALE", .06f, .001f, 2);
        detail_settings.speed = sun::parameter("MW34_WATER_DETAIL_SPEED", .025f, 0, .2f);
        return detail_settings;
    }();
    return value;
}
inline bool enabled(unsigned role) {
    // Transport roles are 1: water, 2: surf, 3: clouds. Unknown roles remain
    // disabled; this predicate does not infer a role from a texture name.
    const auto &detail_settings = settings();
    return (role == 1 || role == 2) ? detail_settings.water && detail_settings.water_strength > 0
           : role == 3              ? detail_settings.clouds && detail_settings.cloud_strength > 0
                                    : false;
}
inline double presentation_time(std::uint64_t frame) {
    // Call this from one presentation thread. Cache one steady-clock
    // sample per supplied frame number so every draw in that frame shares phase.
    // The optional fixed time is also sampled once; neither path advances or
    // reads the guest simulation clock.
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    static const bool fixed = std::getenv("MW34_DETAIL_FIXED_TIME") != nullptr;
    static const float fixed_time = sun::parameter("MW34_DETAIL_FIXED_TIME", 0, 0, 86400);
    static std::uint64_t previous_frame = ~std::uint64_t{};
    static double seconds{};
    if (fixed)
        return fixed_time;
    if (previous_frame != frame) {
        seconds = std::chrono::duration<double>(Clock::now() - start).count();
        previous_frame = frame;
    }
    return seconds;
}
inline void uniform(sun::Uniform &uniform_block, unsigned role, double seconds) {
    // Disabled detail leaves the caller's block untouched. The caller must
    // initialize or clear per-draw detail fields before reusing a uniform block.
    if (!enabled(role))
        return;
    const auto &detail_settings = settings();
    // Integer periodic frequencies in the water tile share this 2000-second
    // phase cycle. Wrapping cannot cause a visible jump, even after long play.
    uniform_block.detail = {
        float(role), role == 3 ? detail_settings.cloud_strength : detail_settings.water_strength,
        detail_settings.scale, 0};
    // Speed is relative to the default; the shader's offsets wrap in full tiles.
    const double travel = seconds * detail_settings.speed;
    uniform_block.detail_motion = {float(travel - std::floor(travel / 50) * 50), 0, 0, 0};
}
} // namespace mw34::detail
