#include "platform/prepared_visuals.h"
#include "frontend/core_critical/renderer/normal_codec.hpp"
#include "frontend/core_critical/renderer/surface_materials.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace platform::prepared {
namespace {

constexpr uint64_t kMaximumPayload = 512ULL * 1024 * 1024;

class Reader {
  public:
    explicit Reader(std::istream &input) : input_(input) {
        const auto start = input.tellg();
        input.seekg(0, std::ios::end);
        const auto end = input.tellg();
        if (start < 0 || end < start || uint64_t(end - start) > kMaximumPayload)
            fail("invalid payload size");
        remaining_ = uint64_t(end - start);
        input.seekg(start);
    }

    [[noreturn]] static void fail(const char *detail) {
        throw std::runtime_error(std::string("Prepared visuals: ") + detail);
    }
    void bytes(void *target, size_t count) {
        if (count > remaining_ || !input_.read(static_cast<char *>(target), std::streamsize(count)))
            fail("truncated payload");
        remaining_ -= count;
    }
    uint32_t u32() {
        uint8_t bytes_[4];
        bytes(bytes_, 4);
        return uint32_t(bytes_[0]) | uint32_t(bytes_[1]) << 8 | uint32_t(bytes_[2]) << 16 |
               uint32_t(bytes_[3]) << 24;
    }
    uint64_t u64() {
        const auto low = u32();
        return low | uint64_t(u32()) << 32;
    }
    float f32() {
        const auto bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value))
            fail("nonfinite vertex or placement");
        return value;
    }
    double f64() {
        const auto bits = u64();
        double value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value))
            fail("nonfinite rig metadata");
        return value;
    }
    std::string name(bool optional = false) {
        char value[32];
        bytes(value, sizeof value);
        const auto *end = static_cast<const char *>(std::memchr(value, 0, sizeof value));
        if (!end || (!optional && end == value))
            fail("invalid payload name");
        for (const char *at = value; at != end; ++at)
            if (uint8_t(*at) < 32 || uint8_t(*at) > 126 || *at == '/' || *at == '\\' || *at == ':')
                fail("unsafe payload name");
        return std::string(value, size_t(end - value));
    }
    Matrix matrix() {
        Matrix result{};
        for (auto &value : result)
            value = f64();
        if (result[12] != 0 || result[13] != 0 || result[14] != 0 || result[15] != 1)
            fail("nonaffine rest matrix");
        const double determinant = result[0] * (result[5] * result[10] - result[6] * result[9]) -
                                   result[1] * (result[4] * result[10] - result[6] * result[8]) +
                                   result[2] * (result[4] * result[9] - result[5] * result[8]);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
            fail("singular rest matrix");
        return result;
    }
    void need(uint64_t count) const {
        if (count > remaining_)
            fail("declared records exceed payload");
    }
    void finish() const {
        if (remaining_)
            fail("trailing payload data");
    }

  private:
    std::istream &input_;
    uint64_t remaining_ = 0;
};

void placement(Reader &reader, std::string &name, std::array<float, 3> &offset) {
    name = reader.name(true);
    for (auto &value : offset)
        value = reader.f32();
    if (name.empty() && offset != std::array<float, 3>{})
        Reader::fail("offset without a frame");
}

Rig read_rig(Reader &reader) {
    Rig rig;
    const auto bones = reader.u32(), references = reader.u32(), flags = reader.u32();
    if (!bones || bones > 256 || !references || references > 128 || flags > 1)
        Reader::fail("invalid rig counts or flags");
    reader.need(40 + uint64_t(bones) * 196 + uint64_t(references) * 160);
    rig.ground_reference = flags != 0;
    rig.hip_drop = reader.f64();
    for (auto &value : rig.walk_x)
        value = reader.f64();
    for (auto &value : rig.walk_z)
        value = reader.f64();
    if (rig.hip_drop < 0 || rig.hip_drop > 10)
        Reader::fail("invalid hip offset");
    std::set<std::string> names, reference_names;
    for (uint32_t i = 0; i < bones; ++i) {
        Bone bone;
        bone.name = reader.name();
        bone.control = reader.name(true);
        const auto parent = reader.u32();
        if (parent != 0xffffffff && parent >= i)
            Reader::fail("invalid bone parent");
        bone.parent = parent == 0xffffffff ? -1 : int32_t(parent);
        bone.rest = reader.matrix();
        if (!names.insert(bone.name).second)
            Reader::fail("duplicate bone name");
        rig.bones.push_back(std::move(bone));
    }
    for (uint32_t i = 0; i < references; ++i) {
        Reference reference;
        reference.name = reader.name();
        reference.rest = reader.matrix();
        if (!reference_names.insert(reference.name).second)
            Reader::fail("duplicate reference name");
        rig.references.push_back(std::move(reference));
    }
    for (const auto &bone : rig.bones)
        if (!bone.control.empty() && !reference_names.count(bone.control))
            Reader::fail("bone names an unknown source control");
    return rig;
}
} // namespace

Mesh read_mesh(std::istream &input) {
    Reader reader(input);
    char magic[8];
    reader.bytes(magic, sizeof magic);
    if (std::string_view(magic, 6) != "BWMESH" || magic[6] < '1' || magic[6] > '6' || magic[7])
        Reader::fail("unsupported mesh format");
    Mesh mesh;
    mesh.version = unsigned(magic[6] - '0');
    const auto count = reader.u32();
    if (!count || count > 256)
        Reader::fail("invalid mesh batch count");
    if (mesh.version == 4)
        mesh.rig = read_rig(reader);
    for (uint32_t i = 0; i < count; ++i) {
        Batch batch;
        batch.owner = reader.name();
        batch.visibility = mesh.version == 1 ? batch.owner : reader.name(true);
        if (mesh.version == 3 || mesh.version >= 5)
            placement(reader, batch.placement_frame, batch.placement_offset);
        if (mesh.version >= 5)
            placement(reader, batch.joint_frame, batch.joint_offset);
        if (mesh.version == 6) {
            placement(reader, batch.anchor_control, batch.anchor_position);
            if (!batch.anchor_control.empty() && batch.placement_frame.empty())
                Reader::fail("anchor without a placement frame");
        }
        if (mesh.version == 4) {
            batch.donor_bone = reader.u32();
            if (batch.donor_bone != 0xffffffff && batch.donor_bone >= mesh.rig.bones.size())
                Reader::fail("invalid donor bone index");
        }
        const auto vertices = reader.u32();
        batch.color = reader.u32();
        if (!vertices || vertices > 65535 || vertices % 3)
            Reader::fail("invalid triangle count");
        reader.need(uint64_t(vertices) * 32);
        batch.vertices.resize(vertices);
        for (auto &vertex : batch.vertices) {
            for (auto &value : vertex)
                value = reader.f32();
            const double normal_length = double(vertex[3]) * vertex[3] +
                                         double(vertex[4]) * vertex[4] +
                                         double(vertex[5]) * vertex[5];
            if (normal_length < 1e-12)
                Reader::fail("degenerate vertex normal");
        }
        if (mesh.version == 4 && batch.donor_bone == 0xffffffff) {
            reader.need(uint64_t(vertices) * 4);
            batch.donor_indices.reserve(vertices);
            for (uint32_t j = 0; j < vertices; ++j) {
                const auto bone = reader.u32();
                if (bone >= mesh.rig.bones.size())
                    Reader::fail("invalid vertex bone index");
                batch.donor_indices.push_back(bone);
            }
        }
        mesh.batches.push_back(std::move(batch));
    }
    reader.finish();
    return mesh;
}

Textures read_textures(std::istream &input) {
    Reader reader(input);
    char magic[8];
    reader.bytes(magic, sizeof magic);
    if (std::string_view(magic, sizeof magic) != std::string_view("BWTEX1\0\0", 8))
        Reader::fail("unsupported texture format");
    const auto batches = reader.u32(), images = reader.u32();
    if (!batches || batches > 256 || !images || images > 256)
        Reader::fail("invalid texture counts");
    reader.need(uint64_t(batches) * 4);
    Textures textures;
    for (uint32_t i = 0; i < batches; ++i) {
        const auto binding = reader.u32();
        if (binding >= images)
            Reader::fail("invalid material binding");
        textures.bindings.push_back(binding);
    }
    std::set<std::string> names;
    for (uint32_t i = 0; i < images; ++i) {
        Image image;
        image.name = reader.name();
        if (!names.insert(image.name).second)
            Reader::fail("duplicate material name");
        image.width = reader.u32();
        image.height = reader.u32();
        image.hash = reader.u64();
        if (!image.width || !image.height || image.width > 4096 || image.height > 4096)
            Reader::fail("invalid texture dimensions");
        const size_t count = size_t(image.width) * image.height;
        reader.need(uint64_t(count) * 2);
        image.pixels.resize(count);
        for (auto &pixel : image.pixels) {
            uint8_t bytes[2];
            reader.bytes(bytes, 2);
            pixel = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
        }
        textures.images.push_back(std::move(image));
    }
    reader.finish();
    return textures;
}

Normals read_normals(std::istream &input, const Textures &textures) {
    Reader reader(input);
    char magic[8];
    reader.bytes(magic, sizeof magic);
    const bool is_bwnrm3 = (std::memcmp(magic, "BWNRM3\0\0", 8) == 0);
    const bool is_bwnrm2 = (std::memcmp(magic, "BWNRM2\0\0", 8) == 0);
    const bool is_bwnrm1 = (std::memcmp(magic, "BWNRM1\0\0", 8) == 0);
    if (!is_bwnrm1 && !is_bwnrm2 && !is_bwnrm3)
        Reader::fail("unsupported normal-map format (expected BWNRM1, BWNRM2, or BWNRM3)");
    const bool version2 = is_bwnrm2 || is_bwnrm3;
    const auto batches = reader.u32(), count = reader.u32();
    if (!batches || batches > 256 || batches != textures.bindings.size() || count > 64)
        Reader::fail("invalid normal-map counts");
    reader.need(uint64_t(batches) * 4);
    Normals normals;
    for (unsigned i = 0; i < batches; ++i) {
        const auto index = reader.u32();
        if (index != no_normal && index >= count)
            Reader::fail("invalid normal-map binding");
        if (!version2 && index == no_normal)
            Reader::fail("invalid normal-map binding");
        normals.bindings.push_back(index);
    }
    std::set<std::string> names;
    uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        NormalImage image;
        image.name = reader.name();
        if (!names.insert(image.name).second)
            Reader::fail("duplicate normal-map material");
        image.width = reader.u32();
        image.height = reader.u32();
        image.hash = reader.u64();
        if (!image.width || !image.height || image.width > 4096 || image.height > 4096)
            Reader::fail("invalid normal-map dimensions");
        const auto pixels = size_t(image.width) * image.height;
        const auto pixel_bytes = uint64_t(pixels) * 4;
        total += pixel_bytes;
        if (total > 256ULL * 1024 * 1024)
            Reader::fail("normal-map decoded budget exceeded");
        image.pixels.resize(pixels);
        if (is_bwnrm3) {
            const auto encoded_size = reader.u64();
            if (!encoded_size || encoded_size > pixel_bytes * 2)
                Reader::fail("invalid compressed normal payload size");
            reader.need(encoded_size);
            std::vector<unsigned char> encoded(static_cast<size_t>(encoded_size));
            reader.bytes(encoded.data(), encoded.size());
            mw34::materials::decode_normal_pixels(
                encoded, reinterpret_cast<unsigned char *>(image.pixels.data()),
                static_cast<size_t>(pixel_bytes));
        } else {
            reader.need(pixel_bytes);
            reader.bytes(image.pixels.data(), static_cast<size_t>(pixel_bytes));
        }
        if (version2) {
            const auto computed_hash =
                mw34::materials::normal_hash(image.pixels.data(), static_cast<size_t>(pixel_bytes));
            if (computed_hash != image.hash)
                Reader::fail("normal-map content hash mismatch");
        }
        normals.images.push_back(std::move(image));
    }
    reader.finish();
    for (size_t i = 0; i < batches; ++i) {
        auto &index = normals.bindings[i];
        if (index == no_normal)
            continue;
        const auto &mat_name = normals.images[index].name;
        if (mat_name != textures.images.at(textures.bindings[i]).name)
            Reader::fail("normal-map/albedo material mismatch");
        if (is_bwnrm1 && (mat_name == "Window" || mat_name == "Glass"))
            index = no_normal;
    }
    return normals;
}

void validate_bindings(const Mesh &mesh, const Textures &textures) {
    if (mesh.batches.size() != textures.bindings.size())
        Reader::fail("mesh/texture batch mismatch");
}

mw34::materials::SurfaceMaterialPack
validate_surface_materials(std::istream &input, const std::string &expected_asset_id,
                           uint64_t expected_mesh_uv_hash,
                           const std::vector<std::string> &expected_batch_materials) {
    return mw34::materials::read_surface_materials(input, expected_asset_id, expected_mesh_uv_hash,
                                                   expected_batch_materials);
}

} // namespace platform::prepared

namespace platform::prepared {
namespace {
bool env_claim_region(std::set<std::string> &regions,
                      const std::string &region) {
  // [WP03B R10: bypass only duplicate-region ownership rejection here.]
  return regions.insert(region).second;
}
} // namespace
} // namespace platform::prepared

#include "prepared_environment.inc"
