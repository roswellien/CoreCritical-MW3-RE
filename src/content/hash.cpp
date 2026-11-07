#include "content/hash.h"
#include "content/content.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cc::content {
namespace {
uint32_t rotate(uint32_t x, unsigned bits) { return (x >> bits) | (x << (32 - bits)); }
constexpr uint32_t constants[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
} // namespace
void Sha256::block(const uint8_t *bytes) {
    uint32_t words[64]{};
    for (unsigned i = 0; i < 16; ++i)
        words[i] = (uint32_t(bytes[4 * i]) << 24) | (uint32_t(bytes[4 * i + 1]) << 16) |
                   (uint32_t(bytes[4 * i + 2]) << 8) | uint32_t(bytes[4 * i + 3]);
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = words[i - 15], y = words[i - 2];
        words[i] = words[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + words[i - 7] +
                   (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
    }
    auto working = state_;
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t a = working[0], b = working[1], c = working[2], d = working[3];
        uint32_t e = working[4], f = working[5], g = working[6], h = working[7];
        uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ ((~e) & g)) +
                      constants[i] + words[i];
        uint32_t t2 =
            (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        working = {{t1 + t2, a, b, c, d + t1, e, f, g}};
    }
    for (unsigned i = 0; i < 8; ++i)
        state_[i] += working[i];
}
void Sha256::update(const void *source, size_t size) {
    auto *bytes = static_cast<const uint8_t *>(source);
    size_ += size;
    while (size) {
        size_t take = std::min(size, pending_.size() - used_);
        std::memcpy(pending_.data() + used_, bytes, take);
        used_ += take;
        bytes += take;
        size -= take;
        if (used_ == pending_.size()) {
            block(pending_.data());
            used_ = 0;
        }
    }
}
std::string Sha256::finish() {
    uint64_t bits = size_ * 8;
    uint8_t last[128]{};
    std::memcpy(last, pending_.data(), used_);
    last[used_] = 0x80;
    size_t count = used_ < 56 ? 64 : 128;
    for (unsigned i = 0; i < 8; ++i)
        last[count - 1 - i] = static_cast<uint8_t>(bits >> (8 * i));
    block(last);
    if (count == 128)
        block(last + 64);
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (uint32_t word : state_)
        result << std::setw(8) << word;
    return result.str();
}
std::string sha256(const std::string &bytes) {
    Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    return hash.finish();
}
std::string file_sha256(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        fail("file-read", "Cannot open payload", {}, {}, path.generic_string());
    Sha256 hash;
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        hash.update(buffer.data(), static_cast<size_t>(input.gcount()));
    }
    if (!input.eof())
        fail("file-read", "Payload read failed", {}, {}, path.generic_string());
    return hash.finish();
}
} // namespace cc::content
