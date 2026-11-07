#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

namespace mw34::capture {
enum class ColorFormat { rgb565, rgba8 };

// Vulkan copies tightly packed source pixels. PNG/video consumers use BGRA8.
// Validate both extents before touching either buffer; never read padding as pixels.
inline void to_bgra(std::span<const std::uint8_t> source, std::span<std::uint8_t> target,
                    ColorFormat format) {
    // The same readback serves held frames, so preserve RGBA8 alpha. RGB565
    // has no alpha channel and its capture pixels are opaque.
    const auto source_bytes_per_pixel = format == ColorFormat::rgba8 ? 4U : 2U;
    if (target.size() % 4 || source.size() != target.size() / 4 * source_bytes_per_pixel)
        throw std::runtime_error("Capture pixel buffer extent mismatch");
    for (std::size_t pixel_index = 0; pixel_index < target.size() / 4; ++pixel_index) {
        auto *destination_pixel = target.data() + pixel_index * 4;
        if (format == ColorFormat::rgba8) {
            const auto *source_pixel = source.data() + pixel_index * 4;
            destination_pixel[0] = source_pixel[2];
            destination_pixel[1] = source_pixel[1];
            destination_pixel[2] = source_pixel[0];
            destination_pixel[3] = source_pixel[3];
        } else {
            const unsigned pixel =
                source[pixel_index * 2] | (unsigned(source[pixel_index * 2 + 1]) << 8);
            destination_pixel[0] = std::uint8_t((pixel & 31) * 255 / 31);
            destination_pixel[1] = std::uint8_t(((pixel >> 5) & 63) * 255 / 63);
            destination_pixel[2] = std::uint8_t(((pixel >> 11) & 31) * 255 / 31);
            destination_pixel[3] = 255;
        }
    }
}
} // namespace mw34::capture
