#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mw34::materials {
// BWNRM3 lossless channel-delta/byte-run codec. Decode directly into the final
// interleaved pixel allocation; malformed tokens cannot grow that allocation.
inline void decode_normal_pixels(const std::vector<unsigned char> &encoded, unsigned char *output,
                                 std::size_t pixel_bytes) {
    auto fail = [] { throw std::runtime_error("Invalid compressed normal pixels"); };
    if (!pixel_bytes || pixel_bytes % 4 || pixel_bytes > 256ULL * 1024 * 1024 || encoded.empty() ||
        encoded.size() > pixel_bytes * 2)
        fail();
    const auto pixels = pixel_bytes / 4;
    std::size_t cursor{}, decoded{};
    unsigned char previous[4]{};
    while (cursor < encoded.size()) {
        const auto token = encoded[cursor++];
        const std::size_t count = (token & ((token & 128) ? 63 : 127)) + 1;
        if (count > pixel_bytes - decoded)
            fail();
        const bool nibble = token >= 192;
        const bool repeat = token >= 128 && !nibble;
        const auto stored = nibble ? (count + 1) / 2 : repeat ? 1 : count;
        if (stored > encoded.size() - cursor)
            fail();
        if (nibble && count % 2 && (encoded[cursor + stored - 1] & 15))
            fail();
        for (std::size_t i = 0; i < count; ++i) {
            const auto channel = decoded / pixels;
            const auto index = decoded % pixels;
            unsigned value = encoded[cursor + (nibble ? i / 2 : repeat ? 0 : i)];
            if (nibble) {
                value = (value >> (i % 2 ? 0 : 4)) & 15;
                if (value >= 8)
                    value += 240;
            }
            previous[channel] = static_cast<unsigned char>(previous[channel] + value);
            output[index * 4 + channel] = previous[channel];
            ++decoded;
        }
        cursor += stored;
    }
    if (decoded != pixel_bytes)
        fail();
}
} // namespace mw34::materials

namespace mw34::normals {
using mw34::materials::decode_normal_pixels;
} // namespace mw34::normals
