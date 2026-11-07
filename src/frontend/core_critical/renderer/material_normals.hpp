#pragma once
#include "normal_codec.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mw34::materials {

inline constexpr std::uint32_t no_normal = 0xffffffffU;
inline constexpr std::uint64_t authored_normal_budget = 1536ULL * 1024 * 1024;
inline constexpr std::uint64_t world_normal_catalog_budget = 4096ULL * 1024 * 1024;
inline constexpr std::uint64_t world_normal_page_file_limit = 16ULL * 1024 * 1024;
inline constexpr std::uint64_t world_normal_lru_resident_limit = 512ULL * 1024 * 1024;
inline constexpr std::uint64_t max_normal_stream_bytes = 256ULL * 1024 * 1024;
inline constexpr std::size_t max_world_normal_pages = 256;
inline constexpr std::size_t max_authored_catalog_pages = 16;
inline constexpr std::size_t max_normal_batches = 256;
inline constexpr std::size_t max_normal_images = 64;

// FNV-1a over the entire byte sequence, shared with the Python sidecar writer.
// This is a format/content key, not a cryptographic publisher-admission check.
inline std::uint64_t normal_hash(const void *data, std::size_t size) {
    auto *bytes = static_cast<const unsigned char *>(data);
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Owns decoded linear RGBA8 pixels. On the supported little-endian Windows
// host, each uint32_t stores the four source channel bytes without colour
// conversion.
struct NormalImage {
    std::string name;
    std::uint32_t width{}, height{};
    std::uint64_t hash{};
    std::vector<std::uint32_t> pixels;
};

struct NormalPack {
    // One image index per material batch, or no_normal for explicit absence.
    // Indices refer to this pack's images, never global renderer texture IDs.
    std::vector<std::uint32_t> bindings;
    std::vector<NormalImage> images;
};

// BWNRM1..4 reader.
// BWNRM2/3 is little-endian RGBA8, linear OpenGL tangent normals.
// Neutral flat normal is (128, 128, 255, 255).
// BWNRM1 remains readable; its glass exception uses its material name, not
// slot 3.
inline NormalPack read_normals(std::istream &input, const std::vector<std::string> &materials,
                               unsigned expected_version = 0) {
    auto fail = [] { throw std::runtime_error("Invalid normal-map sidecar"); };
    auto bytes = [&](void *out, std::size_t count) {
        if (!input.read(static_cast<char *>(out), static_cast<std::streamsize>(count)))
            fail();
    };
    auto u32 = [&] {
        unsigned char b[4];
        bytes(b, 4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
               (std::uint32_t(b[3]) << 24);
    };
    auto u64 = [&] {
        auto lo = u32();
        return std::uint64_t(lo) | (std::uint64_t(u32()) << 32);
    };

    char magic[8];
    bytes(magic, 8);
    const bool shared_rgb = std::memcmp(magic, "BWNRM4\0\0", 8) == 0;
    const bool compressed = shared_rgb || std::memcmp(magic, "BWNRM3\0\0", 8) == 0;
    const bool version2 = compressed || std::memcmp(magic, "BWNRM2\0\0", 8) == 0;
    if (!version2 && std::memcmp(magic, "BWNRM1\0\0", 8) != 0)
        fail();

    const unsigned version = shared_rgb ? 4 : compressed ? 3 : version2 ? 2 : 1;
    if (expected_version && version != expected_version)
        fail();

    const auto batches = u32(), count = u32();
    if (batches != materials.size() || batches > max_normal_batches || count > max_normal_images)
        fail();

    NormalPack pack;
    for (unsigned i = 0; i < batches; ++i) {
        const auto index = u32();
        if (index >= count && !(version2 && index == no_normal))
            fail();
        pack.bindings.push_back(index);
    }

    std::set<std::string> names;
    std::uint64_t total{};
    for (unsigned i = 0; i < count; ++i) {
        char name[32];
        bytes(name, 32);
        if (!name[0] || name[31])
            fail();
        NormalImage image;
        image.name = name;
        if (!names.insert(image.name).second)
            fail();
        image.width = u32();
        image.height = u32();
        image.hash = u64();
        if (!image.width || !image.height || image.width > 4096 || image.height > 4096)
            fail();
        const auto size = std::uint64_t(image.width) * image.height * 4ULL;
        total += size;
        if (total > max_normal_stream_bytes)
            fail();
        image.pixels.resize(std::size_t(size / 4ULL));
        if (compressed) {
            const auto reference = shared_rgb ? u32() : no_normal;
            if (reference != no_normal &&
                (reference >= i || pack.images[reference].width != image.width ||
                 pack.images[reference].height != image.height))
                fail();
            const auto encoded_size = u64();
            if (!encoded_size || encoded_size > size * 2)
                fail();
            std::vector<unsigned char> encoded(static_cast<std::size_t>(encoded_size));
            bytes(encoded.data(), encoded.size());
            decode_normal_pixels(encoded, reinterpret_cast<unsigned char *>(image.pixels.data()),
                                 static_cast<std::size_t>(size));
            if (reference != no_normal) {
                const auto *prior =
                    reinterpret_cast<const unsigned char *>(pack.images[reference].pixels.data());
                auto *pixels = reinterpret_cast<unsigned char *>(image.pixels.data());
                for (std::size_t p = 0; p < size; p += 4)
                    for (unsigned c = 0; c < 3; ++c) {
                        if (pixels[p + c] != 0)
                            fail();
                        pixels[p + c] = prior[p + c];
                    }
            }
        } else {
            bytes(image.pixels.data(), std::size_t(size));
        }
        if (version2 && normal_hash(image.pixels.data(), std::size_t(size)) != image.hash)
            fail();
        pack.images.push_back(std::move(image));
    }
    if (input.peek() != std::char_traits<char>::eof())
        fail();

    for (unsigned i = 0; i < batches; ++i) {
        auto &index = pack.bindings[i];
        if (index == no_normal)
            continue;
        const auto &name = pack.images[index].name;
        if (name != materials[i])
            fail();
        if (!version2 && (name == "Window" || name == "Glass"))
            index = no_normal;
    }
    return pack;
}

// Surface keys identify every source pixel, not a map name, allocation address
// or a short prefix that can alias unrelated textures on another mission.
struct SurfaceKey {
    std::uint32_t width{}, height{}, format{}; // 1 RGB565, 2 ARGB1555, 3 ARGB4444
    std::uint64_t hash{};
    bool operator<(const SurfaceKey &other) const {
        return std::tie(width, height, format, hash) <
               std::tie(other.width, other.height, other.format, other.hash);
    }
    bool operator==(const SurfaceKey &other) const {
        return std::tie(width, height, format, hash) ==
               std::tie(other.width, other.height, other.format, other.hash);
    }
};

struct SurfaceNormals {
    NormalPack pack;
    std::map<SurfaceKey, std::uint32_t> bindings;
};

// Authored surface normal catalog (MW34SNA1 or single BWSNR2/3/4)
inline std::vector<std::filesystem::path>
surface_normal_catalog_paths(const std::filesystem::path &path) {
    auto fail = [] { throw std::runtime_error("Invalid authored surface-normal catalog"); };
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Configured surface normal sidecar is missing");
    char magic[8];
    if (!input.read(magic, 8))
        fail();
    if (!std::memcmp(magic, "BWSNR2\0\0", 8) || !std::memcmp(magic, "BWSNR3\0\0", 8) ||
        !std::memcmp(magic, "BWSNR4\0\0", 8))
        return {path};
    if (std::memcmp(magic, "MW34SNA1", 8) != 0)
        fail();
    if (std::filesystem::file_size(path) > 4096)
        fail();
    if (input.peek() == '\r')
        input.get();
    if (input.get() != '\n')
        fail();

    std::vector<std::filesystem::path> paths;
    std::set<std::string> names;
    std::uint64_t total{};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::filesystem::path entry(line);
        const bool absolute = entry.is_absolute();
        if (line.empty() || line.size() > 512 || line == "." || line == ".." ||
            (!absolute && line.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRST"
                                                 "UVWXYZ0123456789-_.") != std::string::npos) ||
            !names.insert(line).second || names.size() > max_authored_catalog_pages)
            fail();

        auto page = absolute ? entry : path.parent_path() / entry;
        if (!std::filesystem::is_regular_file(page))
            fail();
        for (const auto &previous : paths)
            if (std::filesystem::equivalent(previous, page))
                fail();
        const auto size = std::filesystem::file_size(page);
        total += size;
        if (size > max_normal_stream_bytes || total > authored_normal_budget)
            fail();
        paths.push_back(std::move(page));
    }
    if (!input.eof() || paths.empty())
        fail();
    return paths;
}

inline std::vector<std::pair<SurfaceKey, std::string>>
read_surface_normal_header(std::istream &input, unsigned *version = nullptr) {
    auto fail = [] { throw std::runtime_error("Invalid surface-normal sidecar"); };
    auto bytes = [&](void *out, std::size_t count) {
        if (!input.read(static_cast<char *>(out), static_cast<std::streamsize>(count)))
            fail();
    };
    auto u32 = [&] {
        unsigned char b[4];
        bytes(b, 4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
               (std::uint32_t(b[3]) << 24);
    };

    char magic[8];
    bytes(magic, 8);
    if (std::memcmp(magic, "BWSNR2\0\0", 8) != 0 && std::memcmp(magic, "BWSNR3\0\0", 8) != 0 &&
        std::memcmp(magic, "BWSNR4\0\0", 8) != 0)
        fail();
    if (version)
        *version = !std::memcmp(magic, "BWSNR4\0\0", 8)   ? 4
                   : !std::memcmp(magic, "BWSNR3\0\0", 8) ? 3
                                                          : 2;
    const auto count = u32();
    if (!count || count > max_normal_batches)
        fail();

    std::vector<std::pair<SurfaceKey, std::string>> header;
    std::set<SurfaceKey> unique;
    for (unsigned i = 0; i < count; ++i) {
        SurfaceKey key;
        key.width = u32();
        key.height = u32();
        key.format = u32();
        const auto lo = u32();
        key.hash = std::uint64_t(lo) | (std::uint64_t(u32()) << 32);
        if (!key.width || !key.height || key.width > 4096 || key.height > 4096 || key.format < 1 ||
            key.format > 3 || !unique.insert(key).second)
            fail();
        char name[32];
        bytes(name, 32);
        if (!name[0] || name[31])
            fail();
        header.emplace_back(key, name);
    }
    return header;
}

inline SurfaceNormals read_surface_normals(std::istream &input) {
    unsigned version{};
    const auto header = read_surface_normal_header(input, &version);
    std::vector<std::string> materials;
    materials.reserve(header.size());
    for (const auto &entry : header)
        materials.push_back(entry.second);
    SurfaceNormals result;
    result.pack = read_normals(input, materials, version);
    for (std::size_t i = 0; i < header.size(); ++i)
        result.bindings.emplace(header[i].first, result.pack.bindings[i]);
    return result;
}

// World normal catalog with 512 MiB decoded resident LRU cache and authored
// precedence.
class WorldNormalCache {
  public:
    struct TextureRecord {
        std::string name;
        unsigned width{}, height{};
        std::uint64_t hash{};
        std::vector<std::uint32_t> pixels32;
    };

    struct Page {
        std::filesystem::path path;
        std::set<SurfaceKey> keys;
        bool loaded{false};
        std::vector<int> normal_indices;
        std::map<SurfaceKey, std::uint32_t> local_bindings;
        std::uint64_t pixel_bytes{0};
        std::uint64_t last_use{0};
        bool initialized{false};
    };

    std::vector<TextureRecord> textures;
    std::map<SurfaceKey, int> bindings;
    std::set<SurfaceKey> authored;
    std::vector<Page> pages;
    std::map<SurfaceKey, std::size_t> world_bindings;
    std::map<int, std::size_t> world_images;
    std::uint64_t world_bytes{0};
    std::uint64_t world_package_bytes{0};
    std::uint64_t world_clock{0};

    bool normal_maps_enabled{true};

    explicit WorldNormalCache(bool normal_maps_enabled = true)
        : normal_maps_enabled(normal_maps_enabled) {}

    void register_authored(const SurfaceKey &key, int index) {
        authored.insert(key);
        bindings[key] = index;
        world_bindings.erase(key);
    }

    void load_world_manifest(const std::filesystem::path &manifest_path) {
        std::ifstream list(manifest_path);
        if (!list)
            throw std::runtime_error("Configured world-normal manifest is missing");
        std::string line;
        if (!std::getline(list, line))
            throw std::runtime_error("Empty world-normal manifest");
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line != "MW34SNP1")
            throw std::runtime_error("Invalid world-normal manifest version");

        std::set<std::string> page_names;
        std::set<SurfaceKey> registered_keys;
        std::uint64_t package_bytes{};

        while (std::getline(list, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.size() > 80 || line == "." || line == ".." ||
                line.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                                       "TUVWXYZ0123456789-_.") != std::string::npos ||
                !page_names.insert(line).second || page_names.size() > max_world_normal_pages)
                throw std::runtime_error("Invalid world-normal page name/count");

            Page page;
            page.path = manifest_path.parent_path() / line;
            if (!std::filesystem::is_regular_file(page.path))
                throw std::runtime_error("World-normal page is missing");

            const auto size = std::filesystem::file_size(page.path);
            package_bytes += size;
            if (size > world_normal_page_file_limit ||
                package_bytes >
                    world_normal_catalog_budget - world_package_bytes ||
                pages.size() >= max_world_normal_pages)
              throw std::runtime_error(
                  "World-normal package exceeds byte budget");

            std::ifstream input(page.path, std::ios::binary);
            if (!input)
                throw std::runtime_error("World-normal page cannot be opened");
            auto header = read_surface_normal_header(input);
            for (const auto &entry : header) {
                if (!registered_keys.insert(entry.first).second || registered_keys.size() > 16384)
                    throw std::runtime_error("Duplicate/excess world-normal texture keys");
                page.keys.insert(entry.first);
                // Authored catalog bindings/exclusions take precedence.
                if (!authored.count(entry.first))
                    world_bindings.emplace(entry.first, pages.size());
            }
            pages.push_back(std::move(page));
        }
        if (!list.eof() || page_names.empty())
          throw std::runtime_error("Incomplete world-normal manifest");
        world_package_bytes += package_bytes;
    }

    void load_page(std::size_t page_index,
                   std::uint64_t pixel_budget = world_normal_lru_resident_limit) {
        if (page_index >= pages.size())
            throw std::runtime_error("Page index out of bounds");
        auto &page = pages[page_index];
        page.last_use = ++world_clock;
        if (pixel_budget > world_normal_lru_resident_limit)
            throw std::runtime_error("World-normal resident pixel budget exceeds limit");
        if (page.loaded) {
            if (world_bytes > pixel_budget)
                throw std::runtime_error("Loaded world normals exceed requested pixel budget");
            return;
        }

        std::ifstream input(page.path, std::ios::binary);
        if (!input)
            throw std::runtime_error("World-normal page disappeared");
        auto parsed = read_surface_normals(input);
        if (parsed.bindings.size() != page.keys.size())
            throw std::runtime_error("World-normal page keys changed");
        for (const auto &entry : parsed.bindings)
            if (!page.keys.count(entry.first))
                throw std::runtime_error("World-normal page identity changed");

        std::uint64_t page_pixel_bytes{};
        for (const auto &image : parsed.pack.images)
            page_pixel_bytes += std::uint64_t(image.pixels.size()) * 4ULL;

        if (page.initialized) {
            if (page.normal_indices.size() != parsed.pack.images.size() ||
                page.pixel_bytes != page_pixel_bytes)
                throw std::runtime_error("World-normal page payload changed after eviction");
            for (std::size_t i = 0; i < parsed.pack.images.size(); ++i) {
                const auto &before = textures.at(static_cast<std::size_t>(page.normal_indices[i]));
                const auto &after = parsed.pack.images[i];
                if (before.name != after.name || before.width != after.width ||
                    before.height != after.height || before.hash != after.hash)
                    throw std::runtime_error("World-normal image changed after eviction");
            }
            for (const auto &[key, index] : parsed.bindings)
                if (page.local_bindings.at(key) != index)
                    throw std::runtime_error("World-normal binding changed after eviction");
        }

        if (pixel_budget > world_normal_lru_resident_limit || page_pixel_bytes > pixel_budget)
            throw std::runtime_error("World-normal page exceeds resident pixel budget");

        // Evict LRU pages if needed to fit under pixel_budget
        while (world_bytes > pixel_budget - page_pixel_bytes) {
            std::size_t victim = pages.size();
            for (std::size_t i = 0; i < pages.size(); ++i) {
                const auto &candidate = pages[i];
                if (i != page_index && candidate.loaded && candidate.pixel_bytes &&
                    (victim == pages.size() || candidate.last_use < pages[victim].last_use))
                    victim = i;
            }
            if (victim == pages.size())
                throw std::runtime_error("World-normal resident byte accounting is inconsistent");
            auto &old = pages[victim];
            for (int index : old.normal_indices)
                std::vector<std::uint32_t>().swap(
                    textures.at(static_cast<std::size_t>(index)).pixels32);
            world_bytes -= old.pixel_bytes;
            old.loaded = false;
        }

        if (!page.initialized) {
            if (textures.size() + parsed.pack.images.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("World-normal index overflow");
            for (const auto &image : parsed.pack.images) {
                const int index = static_cast<int>(textures.size());
                TextureRecord record{};
                record.name = image.name;
                record.width = image.width;
                record.height = image.height;
                record.hash = image.hash;
                textures.push_back(std::move(record));
                page.normal_indices.push_back(index);
                world_images.emplace(index, page_index);
            }
            page.local_bindings = parsed.bindings;
            page.pixel_bytes = page_pixel_bytes;
            page.initialized = true;
        }

        for (std::size_t i = 0; i < parsed.pack.images.size(); ++i)
            textures.at(static_cast<std::size_t>(page.normal_indices[i])).pixels32 =
                std::move(parsed.pack.images[i].pixels);

        for (const auto &[key, normal_index] : parsed.bindings) {
            // Authored bindings and explicit exclusions take precedence!
            const auto owner = world_bindings.find(key);
            if (!authored.count(key) && owner != world_bindings.end() && owner->second == page_index)
                bindings[key] =
                    (normal_index == no_normal) ? -1 : page.normal_indices.at(normal_index);
        }

        world_bytes += page_pixel_bytes;
        page.loaded = true;
    }

    int surface_normal_index(const SurfaceKey &key, bool enabled = true) {
        if (!enabled || !normal_maps_enabled)
            return -1;
        const auto it = bindings.find(key);
        if (it != bindings.end()) {
            if (it->second >= 0)
                require_image(it->second);
            return it->second;
        }
        const auto w_it = world_bindings.find(key);
        if (w_it != world_bindings.end()) {
            load_page(w_it->second);
            const auto loaded_it = bindings.find(key);
            if (loaded_it != bindings.end())
                return loaded_it->second;
            return -1;
        }
        return -1;
    }

    void require_image(int index) {
        if (index < 0 || !normal_maps_enabled)
            return;
        const auto found = world_images.find(index);
        if (found != world_images.end())
            load_page(found->second);
    }
};

} // namespace mw34::materials

namespace mw34::normals {
using namespace mw34::materials;
} // namespace mw34::normals
