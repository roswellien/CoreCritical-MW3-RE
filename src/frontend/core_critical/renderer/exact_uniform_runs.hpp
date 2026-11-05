#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace mw34::submission {

// Exact identities for consecutive uniform values within one scene. These are
// run numbers, not hashes: equal IDs guarantee byte-identical shader inputs.
// Keep the comparison in ordinary CPU memory; upload memory may be uncached.
template <class Uniform> class ExactUniformRuns {
    static_assert(std::is_trivially_copyable_v<Uniform>);
    Uniform previous{};
    std::uint32_t current{};

  public:
    std::uint32_t identify(const Uniform& value) {
        if (!current || std::memcmp(&previous, &value, sizeof(value)) != 0) {
            if (current == std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Uniform run identity overflow");
            std::memcpy(&previous, &value, sizeof(value));
            ++current;
        }
        return current;
    }
};

} // namespace mw34::submission
