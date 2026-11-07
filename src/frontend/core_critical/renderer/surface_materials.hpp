#pragma once
#include "material_normals.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mw34::materials {

inline constexpr std::uint32_t no_surface_material = 0xffffffffU;
inline constexpr std::uint32_t no_surface_image = 0xffffffffU;

inline constexpr std::uint32_t surface_role_height = 1U;
inline constexpr std::uint32_t surface_role_thickness = 2U;

inline constexpr std::uint32_t surface_address_wrap = 1U;
inline constexpr std::uint32_t surface_address_mirror = 2U;
inline constexpr std::uint32_t surface_address_clamp = 3U;

inline constexpr std::size_t max_surface_batches = 4096U;
inline constexpr std::size_t max_surface_records = 4096U;
inline constexpr std::size_t max_surface_images = 512U;
inline constexpr std::size_t max_surface_identifier_bytes = 4096U;
inline constexpr std::size_t max_surface_material_name_bytes = 31U;
inline constexpr std::uint32_t max_surface_dimension = 4096U;
inline constexpr std::uint64_t max_surface_sidecar_bytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr float max_surface_world_amplitude = 16.0f;
inline constexpr float max_surface_uv_transform = 1024.0f;

// Owns decoded linear RGBA8 pixels where the red channel carries scalar height
// or thickness. Normal-map alpha is never modified or repurposed for these
// channels.
struct SurfaceMaterialImage {
    std::string name;
    std::uint32_t role{};
    std::uint32_t width{}, height{};
    std::uint64_t hash{};
    std::vector<std::uint32_t> pixels;
};

// Immutable validated surface-material parameters in native host world units.
// Shared from OverrideBatch into ModelInstance and Draw without changing the
// existing vertex or push-constant ABI until rendering stages are enabled.
struct SurfaceMaterialRecord {
    std::string asset_id;
    std::string material_name;
    std::uint32_t height_image{no_surface_image};
    std::uint32_t thickness_image{no_surface_image};
    float height_reference{0.0f};
    float height_amplitude{0.0f};
    float max_displacement{0.0f};
    std::uint32_t tessellation_cap{1U};
    std::array<float, 2> uv_scale{1.0f, 1.0f};
    std::array<float, 2> uv_offset{0.0f, 0.0f};
    std::uint32_t address_mode_u{surface_address_wrap};
    std::uint32_t address_mode_v{surface_address_wrap};
    std::array<float, 4> atlas_rect{0.0f, 0.0f, 1.0f, 1.0f};
    float sss_participation{0.0f};
    std::array<float, 3> sss_tint{0.0f, 0.0f, 0.0f};
    float sss_radius{0.0f};
    float thickness_scale{0.0f};
    float roughness{0.7f};
    float reflectivity{0.04f};
    float metallic{0.0f};
    // Resolved host-wide image table indices (-1 when absent).
    int host_height_texture{-1};
    int host_thickness_texture{-1};
};

struct SurfaceMaterialPack {
    std::uint32_t version{1U};
    std::uint64_t mesh_uv_hash{};
    std::vector<std::uint32_t> bindings;
    std::vector<std::shared_ptr<SurfaceMaterialRecord>> records;
    std::vector<SurfaceMaterialImage> images;
};

inline bool valid_surface_identifier(const std::string &value, std::size_t max_bytes,
                                     bool allow_colon) {
    if (value.empty() || value.size() > max_bytes || value.find("..") != std::string::npos)
        return false;
    for (char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = (c >= '0' && c <= '9');
        const bool symbol = c == '_' || c == '.' || c == '-' || (allow_colon && c == ':');
        if (!alpha && !digit && !symbol)
            return false;
    }
    return true;
}

// Compute the 64-bit FNV-1a geometry/UV compatibility hash over an in-memory
// sequence of batches, matching Python surface_materials.mesh_uv_identity().
template <typename BatchRange>
inline std::uint64_t compute_mesh_uv_hash(const BatchRange &batches, std::size_t first,
                                          std::size_t end) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    auto fold = [&](const void *data, std::size_t size) {
        const auto *bytes = static_cast<const unsigned char *>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 0x100000001b3ULL;
        }
    };
    const auto count = static_cast<std::uint32_t>(end - first);
    fold(&count, sizeof(count));
    for (std::size_t at = first; at < end; ++at) {
        const auto &vertices = batches[at].vertices;
        const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
        fold(&vertex_count, sizeof(vertex_count));
        if (!vertices.empty())
            fold(vertices.data(), vertices.size() * sizeof(vertices[0]));
    }
    return hash;
}

inline SurfaceMaterialPack
read_surface_materials(std::istream &input, const std::string &expected_asset_id,
                       std::uint64_t expected_mesh_uv_hash,
                       const std::vector<std::string> &expected_batch_materials,
                       std::uint64_t max_bytes = max_surface_sidecar_bytes) {
    auto fail = [] { throw std::runtime_error("Invalid surface-material sidecar"); };
    if (!max_bytes || max_bytes > max_surface_sidecar_bytes || expected_batch_materials.empty() ||
        expected_batch_materials.size() > max_surface_batches)
        fail();
    if (!expected_asset_id.empty() &&
        !valid_surface_identifier(expected_asset_id, max_surface_identifier_bytes, true))
        fail();
    for (const auto &mat_name : expected_batch_materials) {
        if (!valid_surface_identifier(mat_name, max_surface_material_name_bytes, false))
            fail();
    }

    std::uint64_t consumed_bytes = 0;
    auto bytes = [&](void *out, std::size_t count) {
        if (count > max_bytes - consumed_bytes)
            fail();
        if (!input.read(static_cast<char *>(out), static_cast<std::streamsize>(count)))
            fail();
        consumed_bytes += count;
    };
    auto u32 = [&] {
        unsigned char b[4];
        bytes(b, 4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
               (std::uint32_t(b[3]) << 24);
    };
    auto u64 = [&] {
        const auto lo = u32();
        return std::uint64_t(lo) | (std::uint64_t(u32()) << 32);
    };
    auto f32 = [&](float low, float high) {
        const auto bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value) || value < low || value > high)
            fail();
        return value;
    };
    auto text = [&](std::size_t max_len, bool allow_colon) {
        const auto len = u32();
        if (!len || len > max_len)
            fail();
        std::string value(len, '\0');
        bytes(value.data(), len);
        if (!valid_surface_identifier(value, max_len, allow_colon))
            fail();
        return value;
    };

    char magic[8];
    bytes(magic, 8);
    if (std::memcmp(magic, "BWSFM1\0\0", 8) != 0)
        fail();

    SurfaceMaterialPack pack;
    pack.version = u32();
    const auto batch_count = u32();
    const auto record_count = u32();
    const auto image_count = u32();
    pack.mesh_uv_hash = u64();

    if (pack.version != 1U || batch_count != expected_batch_materials.size() ||
        record_count > max_surface_records || image_count > max_surface_images ||
        pack.mesh_uv_hash != expected_mesh_uv_hash)
        fail();

    pack.bindings.reserve(batch_count);
    for (std::uint32_t i = 0; i < batch_count; ++i) {
        const auto index = u32();
        if (index != no_surface_material && index >= record_count)
            fail();
        pack.bindings.push_back(index);
    }

    pack.records.reserve(record_count);
    std::pair<std::string, std::string> previous_key;
    bool has_previous_key = false;
    for (std::uint32_t i = 0; i < record_count; ++i) {
        auto record_ptr = std::make_shared<SurfaceMaterialRecord>();
        auto &record = *record_ptr;
        record.asset_id = text(max_surface_identifier_bytes, true);
        if (!expected_asset_id.empty() && record.asset_id != expected_asset_id)
            fail();
        record.material_name = text(max_surface_material_name_bytes, false);

        const std::pair<std::string, std::string> key{record.asset_id, record.material_name};
        if (has_previous_key && key <= previous_key)
            fail();
        previous_key = key;
        has_previous_key = true;

        record.height_image = u32();
        record.thickness_image = u32();
        if (record.height_image != no_surface_image && record.height_image >= image_count)
            fail();
        if (record.thickness_image != no_surface_image && record.thickness_image >= image_count)
            fail();

        record.height_reference = f32(0.0f, 1.0f);
        record.height_amplitude = f32(-max_surface_world_amplitude, max_surface_world_amplitude);
        record.max_displacement = f32(0.0f, max_surface_world_amplitude);
        if (record.max_displacement < std::abs(record.height_amplitude))
            fail();
        if (record.height_image == no_surface_image &&
            (record.height_amplitude != 0.0f || record.max_displacement != 0.0f))
            fail();

        record.tessellation_cap = u32();
        if (record.tessellation_cap < 1U || record.tessellation_cap > 8U ||
            (record.height_image == no_surface_image && record.tessellation_cap != 1U))
            fail();

        record.uv_scale[0] = f32(-max_surface_uv_transform, max_surface_uv_transform);
        record.uv_scale[1] = f32(-max_surface_uv_transform, max_surface_uv_transform);
        if (record.uv_scale[0] == 0.0f || record.uv_scale[1] == 0.0f)
            fail();
        record.uv_offset[0] = f32(-max_surface_uv_transform, max_surface_uv_transform);
        record.uv_offset[1] = f32(-max_surface_uv_transform, max_surface_uv_transform);

        record.address_mode_u = u32();
        record.address_mode_v = u32();
        if (record.address_mode_u < surface_address_wrap ||
            record.address_mode_u > surface_address_clamp ||
            record.address_mode_v < surface_address_wrap ||
            record.address_mode_v > surface_address_clamp)
            fail();

        for (float &v : record.atlas_rect)
            v = f32(0.0f, 1.0f);
        if (!(record.atlas_rect[0] < record.atlas_rect[2] &&
              record.atlas_rect[1] < record.atlas_rect[3]))
            fail();

        record.sss_participation = f32(0.0f, 1.0f);
        for (float &v : record.sss_tint)
            v = f32(0.0f, 1.0f);
        record.sss_radius = f32(0.0f, max_surface_world_amplitude);
        record.thickness_scale = f32(0.0f, max_surface_world_amplitude);
        if (record.sss_participation == 0.0f &&
            (record.sss_radius != 0.0f || record.thickness_scale != 0.0f ||
             record.thickness_image != no_surface_image))
            fail();

        record.roughness = f32(0.0f, 1.0f);
        record.reflectivity = f32(0.0f, 1.0f);
        record.metallic = f32(0.0f, 1.0f);

        pack.records.push_back(std::move(record_ptr));
    }

    pack.images.reserve(image_count);
    std::string previous_image_name;
    bool has_previous_image = false;
    std::uint64_t decoded_image_bytes = 0;
    for (std::uint32_t i = 0; i < image_count; ++i) {
        SurfaceMaterialImage image;
        image.name = text(max_surface_identifier_bytes, true);
        if (has_previous_image && image.name <= previous_image_name)
            fail();
        previous_image_name = image.name;
        has_previous_image = true;

        image.role = u32();
        image.width = u32();
        image.height = u32();
        image.hash = u64();
        if ((image.role != surface_role_height && image.role != surface_role_thickness) ||
            !image.width || !image.height || image.width > max_surface_dimension ||
            image.height > max_surface_dimension)
            fail();

        const auto pixel_bytes = std::uint64_t(image.width) * image.height * 4ULL;
        if (pixel_bytes > max_bytes - decoded_image_bytes)
            fail();
        decoded_image_bytes += pixel_bytes;

        image.pixels.resize(static_cast<std::size_t>(pixel_bytes / 4ULL));
        bytes(image.pixels.data(), static_cast<std::size_t>(pixel_bytes));
        if (normal_hash(image.pixels.data(), static_cast<std::size_t>(pixel_bytes)) != image.hash)
            fail();
        pack.images.push_back(std::move(image));
    }

    if (input.peek() != std::char_traits<char>::eof())
        fail();

    for (const auto &rec_ptr : pack.records) {
        const auto &record = *rec_ptr;
        if (record.height_image != no_surface_image &&
            pack.images[record.height_image].role != surface_role_height)
            fail();
        if (record.thickness_image != no_surface_image &&
            pack.images[record.thickness_image].role != surface_role_thickness)
            fail();
    }

    for (std::uint32_t i = 0; i < batch_count; ++i) {
        const auto rec_index = pack.bindings[i];
        if (rec_index == no_surface_material)
            continue;
        if (pack.records[rec_index]->material_name != expected_batch_materials[i])
            fail();
    }

    return pack;
}

} // namespace mw34::materials

namespace mw34::surface_materials {
using namespace mw34::materials;
} // namespace mw34::surface_materials
