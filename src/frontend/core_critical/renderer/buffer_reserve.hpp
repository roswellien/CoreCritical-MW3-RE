#pragma once
#include <cstdint>

namespace mw34::allocation {
// Only growable streaming buffers receive spare capacity. Immutable model
// staging allocations keep their exact initial extent. Bound extra space to
// less than 64 KiB per buffer and leave large transfers at their requested size.
inline constexpr std::uint64_t stream_capacity(std::uint64_t required) noexcept {
    constexpr std::uint64_t quantum = 64 * 1024, limit = 16 * 1024 * 1024;
    if (required >= limit)
        return required;
    return (required + quantum - 1) & ~(quantum - 1);
}
} // namespace mw34::allocation
