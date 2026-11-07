#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/prepared_environment_bank.hpp"
#include "platform/prepared_visuals.h"
#include "platform/render3d.h"
#include "platform/sha256.hpp"
#include "prepared_visual_fixture.h"
#include <nlohmann-json/json.hpp>

#include "frontend/core_critical/renderer/material_normals.hpp"
#include "frontend/core_critical/renderer/normal_codec.hpp"
#include "frontend/core_critical/renderer/surface_materials.hpp"

#include <atomic>
#include <chrono>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <array>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static std::filesystem::path make_private_test_dir(const char *prefix) {
    static std::atomic<std::uint32_t> s_seq{0};
    const auto pid =
#if defined(_WIN32)
        ::_getpid();
#else
        ::getpid();
#endif
    const auto seq = ++s_seq;
    const auto tick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto p = std::filesystem::temp_directory_path() /
                   (std::string(prefix) + "_" + std::to_string(pid) + "_" + std::to_string(seq) +
                    "_" + std::to_string(tick));
    std::filesystem::create_directories(p);
    return p;
}

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

using prepared_fixture::mesh;
using prepared_fixture::textures;

template <class Function> void rejects(const std::string &data, Function read) {
    bool rejected = false;
    try {
        std::istringstream input(data);
        read(input);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Malformed prepared payload was accepted");
}

void write_u32(std::string &out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffU));
}

void write_u64(std::string &out, std::uint64_t value) {
    write_u32(out, static_cast<std::uint32_t>(value));
    write_u32(out, static_cast<std::uint32_t>(value >> 32));
}

void write_f32(std::string &out, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    write_u32(out, bits);
}

void write_str(std::string &out, const std::string &value) {
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out.append(value);
}

struct TestVertex {
    float x{}, y{}, z{};
    float nx{}, ny{}, nz{};
    float u{}, v{};
};

struct TestBatch {
    std::vector<TestVertex> vertices;
};

std::string build_bwsfm1(std::uint64_t mesh_hash, float height_amp, float max_disp,
                         float height_ref = 0.5f, std::uint32_t height_img = 0U,
                         std::uint32_t thick_img = mw34::materials::no_surface_image,
                         std::uint32_t tess_cap = 4U, float sss_part = 0.0f, float sss_rad = 0.0f,
                         float thick_scale = 0.0f, float roughness = 0.7f,
                         float reflectivity = 0.04f, float metallic = 0.0f) {
    using namespace mw34::materials;
    const std::array<unsigned char, 16> h_pixels{
        32, 0, 0, 255, 96, 0, 0, 255, 160, 0, 0, 255, 224, 0, 0, 255,
    };
    const std::array<unsigned char, 16> t_pixels{
        64, 0, 0, 255, 128, 0, 0, 255, 192, 0, 0, 255, 240, 0, 0, 255,
    };

    std::string out("BWSFM1\0\0", 8);
    write_u32(out, 1U);        // version
    write_u32(out, 2U);        // batch_count
    write_u32(out, 2U);        // record_count
    write_u32(out, 2U);        // image_count
    write_u64(out, mesh_hash); // mesh_uv_hash

    // Batch bindings: batch 0 -> record 0, batch 1 -> record 1
    write_u32(out, 0U);
    write_u32(out, 1U);

    // Record 0: ("bushwacker:close", "armor_hull")
    write_str(out, "bushwacker:close");
    write_str(out, "armor_hull");
    write_u32(out, height_img);
    write_u32(out, thick_img);
    write_f32(out, height_ref);
    write_f32(out, height_amp);
    write_f32(out, max_disp);
    write_u32(out, tess_cap);
    write_f32(out, 2.0f);  // uv_scale[0]
    write_f32(out, -2.0f); // uv_scale[1]
    write_f32(out, 0.125f);
    write_f32(out, -0.25f);
    write_u32(out, surface_address_mirror);
    write_u32(out, surface_address_clamp);
    write_f32(out, 0.125f);
    write_f32(out, 0.25f);
    write_f32(out, 0.875f);
    write_f32(out, 0.75f);
    write_f32(out, sss_part);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, sss_rad);
    write_f32(out, thick_scale);
    write_f32(out, roughness);
    write_f32(out, reflectivity);
    write_f32(out, metallic);

    // Record 1: ("bushwacker:close", "canopy_glass")
    write_str(out, "bushwacker:close");
    write_str(out, "canopy_glass");
    write_u32(out, no_surface_image);
    write_u32(out, 1U); // thickness_image
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_u32(out, 1U);
    write_f32(out, 1.0f);
    write_f32(out, 1.0f);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_u32(out, surface_address_wrap);
    write_u32(out, surface_address_wrap);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, 1.0f);
    write_f32(out, 1.0f);
    write_f32(out, 0.5f);
    write_f32(out, 0.25f);
    write_f32(out, 0.5f);
    write_f32(out, 0.75f);
    write_f32(out, 0.125f);
    write_f32(out, 0.375f);
    write_f32(out, 0.7f);
    write_f32(out, 0.04f);
    write_f32(out, 0.0f);

    // Image 0: height_armor
    write_str(out, "height_armor");
    write_u32(out, surface_role_height);
    write_u32(out, 2U);
    write_u32(out, 2U);
    write_u64(out, normal_hash(h_pixels.data(), h_pixels.size()));
    out.append(reinterpret_cast<const char *>(h_pixels.data()), h_pixels.size());

    // Image 1: thick_canopy
    write_str(out, "thick_canopy");
    write_u32(out, surface_role_thickness);
    write_u32(out, 2U);
    write_u32(out, 2U);
    write_u64(out, normal_hash(t_pixels.data(), t_pixels.size()));
    out.append(reinterpret_cast<const char *>(t_pixels.data()), t_pixels.size());

    return out;
}

bool rejected_surface_materials(const std::string &bytes, const std::string &asset_id,
                                std::uint64_t mesh_hash,
                                const std::vector<std::string> &materials) {
    try {
        std::istringstream stream(bytes);
        mw34::materials::read_surface_materials(stream, asset_id, mesh_hash, materials);
        return false;
    } catch (const std::runtime_error &) {
        return true;
    }
}

void check_normal_codec() {
    using namespace mw34::materials;

    // 1. Round-trip test of decode_normal_pixels covering literal, repeat, and
    // nibble tokens. We decode 4 pixels (16 bytes) across 4 channels (R, G, B,
    // A). Channel 0 (R): 1 pixel delta 128 (literal run token 0, count 1), then 3
    // pixels delta 0 (repeat token 128+2=130, count 3) -> 128, 128, 128, 128
    // Channel 1 (G): 4 pixels via literal deltas (token 3, count 4): [100, 20, 5,
    // 2] -> 100, 120, 125, 127 Channel 2 (B): 4 pixels via nibble deltas (token
    // 192+3=195, count 4): [0x12, 0xf0] -> 1, 2, -1(255), 0 -> 1, 3, 2, 2 Channel
    // 3 (A): 1 pixel delta 255 (literal token 0), then 3 pixels delta 0 (repeat
    // token 130) -> 255, 255, 255, 255
    const std::vector<unsigned char> encoded{
        0,   128,  130,  0,    // Channel 0 (R)
        3,   100,  20,   5, 2, // Channel 1 (G)
        195, 0x12, 0xf0,       // Channel 2 (B)
        0,   255,  130,  0     // Channel 3 (A)
    };

    const std::vector<unsigned char> expected{
        128, 100, 1, 255, // Pixel 0 (RGBA)
        128, 120, 3, 255, // Pixel 1 (RGBA)
        128, 125, 2, 255, // Pixel 2 (RGBA)
        128, 127, 2, 255  // Pixel 3 (RGBA)
    };

    std::vector<unsigned char> decoded(16, 0);
    decode_normal_pixels(encoded, decoded.data(), 16);

    require(decoded == expected, "decode_normal_pixels bit-exact RGBA8 mismatch");
    require(normal_hash(decoded.data(), 16) == normal_hash(expected.data(), 16),
            "FNV-1a64 hash mismatch on decoded pixels");

    // Verify alpha is preserved and intact
    for (std::size_t p = 0; p < 4; ++p) {
        require(decoded[p * 4 + 3] == 255, "Alpha channel was not preserved");
    }

    // Error cases: invalid arguments / corrupted streams fail closed
    std::vector<unsigned char> out_buf(16);
    bool rejected = false;
    try {
        decode_normal_pixels({}, out_buf.data(), 16);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Empty encoded buffer was accepted");

    rejected = false;
    try {
        decode_normal_pixels(encoded, out_buf.data(), 15); // not multiple of 4
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Unaligned pixel bytes was accepted");

    rejected = false;
    try {
        auto bad_enc = encoded;
        bad_enc.pop_back(); // truncated
        decode_normal_pixels(bad_enc, out_buf.data(), 16);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Truncated compressed stream was accepted");

    rejected = false;
    try {
        auto bad_enc = encoded;
        bad_enc.push_back(0); // trailing
        decode_normal_pixels(bad_enc, out_buf.data(), 16);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Trailing byte in compressed stream was accepted");
}

void check_bwnrm_formats() {
    using namespace mw34::materials;

    // BWNRM1 test: material name Window/Glass maps to no_normal
    {
        std::string stream("BWNRM1\0\0", 8);
        write_u32(stream, 2U); // batches
        write_u32(stream, 2U); // count
        write_u32(stream, 0U); // binding 0 -> image 0
        write_u32(stream, 1U); // binding 1 -> image 1
        std::string img0 = "Armor";
        img0.resize(32, '\0');
        stream.append(img0);
        write_u32(stream, 1U);
        write_u32(stream, 1U);
        write_u64(stream, 0ULL); // hash (ignored in v1)
        const unsigned char px0[]{128, 128, 255, 255};
        stream.append(reinterpret_cast<const char *>(px0), 4);

        std::string img1 = "Glass";
        img1.resize(32, '\0');
        stream.append(img1);
        write_u32(stream, 1U);
        write_u32(stream, 1U);
        write_u64(stream, 0ULL);
        const unsigned char px1[]{128, 128, 255, 255};
        stream.append(reinterpret_cast<const char *>(px1), 4);

        std::istringstream iss(stream);
        const auto pack = read_normals(iss, {"Armor", "Glass"});
        require(pack.bindings[0] == 0U, "BWNRM1 normal binding 0 mismatch");
        require(pack.bindings[1] == no_normal, "BWNRM1 Glass was not mapped to no_normal");
    }

    // BWNRM3 test: compressed normal payload decoded via decode_normal_pixels
    {
        std::string stream("BWNRM3\0\0", 8);
        write_u32(stream, 1U); // batches
        write_u32(stream, 1U); // count
        write_u32(stream, 0U); // binding 0
        std::string name = "Armor";
        name.resize(32, '\0');
        stream.append(name);
        write_u32(stream, 1U);
        write_u32(stream, 1U);
        const unsigned char pixel[]{128, 128, 255, 255};
        const auto hash = normal_hash(pixel, 4);
        write_u64(stream, hash);
        // Compressed payload: literal token 3 (count 4), 4 deltas
        const unsigned char encoded[]{3, 128, 128, 255, 255};
        write_u64(stream, sizeof(encoded));
        stream.append(reinterpret_cast<const char *>(encoded), sizeof(encoded));

        std::istringstream iss(stream);
        const auto pack = read_normals(iss, {"Armor"});
        require(pack.images.size() == 1, "BWNRM3 image count mismatch");
        require(pack.images[0].pixels[0] == 0xffff8080U, "BWNRM3 pixel mismatch");
        require(pack.images[0].hash == hash, "BWNRM3 hash mismatch");

        // Corrupted hash fails closed
        auto bad = stream;
        bad[60] ^= 1; // alter hash byte at offset 60
        std::istringstream bad_iss(bad);
        bool failed = false;
        try {
            read_normals(bad_iss, {"Armor"});
        } catch (const std::runtime_error &) {
            failed = true;
        }
        require(failed, "BWNRM3 corrupted hash was accepted");
    }
}

void check_surface_materials_contract() {
    using namespace mw34::materials;

    // 1. Geometry batch fixture and mesh_uv_hash computation
    std::vector<TestBatch> batches(2);
    for (std::size_t b = 0; b < 2; ++b) {
        const float u_step = (b == 0) ? 0.25f : 0.75f;
        for (int idx = 0; idx < 3; ++idx) {
            batches[b].vertices.push_back(TestVertex{float(idx), float(idx + 1), float(idx + 2),
                                                     0.0f, 0.0f, 1.0f, u_step * float(idx),
                                                     0.5f * float(idx)});
        }
    }

    const auto mesh_hash = compute_mesh_uv_hash(batches, 0, batches.size());
    require(mesh_hash != 0, "compute_mesh_uv_hash produced 0");

    // Modifying UV must alter mesh_uv_hash
    auto modified_batches = batches;
    modified_batches[1].vertices[2].v = 0.999f;
    require(compute_mesh_uv_hash(modified_batches, 0, modified_batches.size()) != mesh_hash,
            "Altering UV did not change mesh_uv_hash");

    const std::vector<std::string> materials{"armor_hull", "canopy_glass"};

    // 2. Signed height amplitude tests:
    // A) Negative amplitude (-4.0f) with max_displacement = 4.0f (valid: 4.0 >=
    // abs(-4.0))
    {
        const auto sidecar = build_bwsfm1(mesh_hash, -4.0f, 4.0f, 0.5f);
        std::istringstream iss(sidecar);
        const auto pack = read_surface_materials(iss, "bushwacker:close", mesh_hash, materials);
        require(pack.records[0]->height_amplitude == -4.0f, "Negative height_amplitude mismatch");
        require(pack.records[0]->max_displacement == 4.0f, "max_displacement mismatch");
        require(pack.records[0]->roughness == 0.7f, "Default roughness mismatch");
        require(pack.records[0]->reflectivity == 0.04f, "Default reflectivity mismatch");
        require(pack.records[0]->metallic == 0.0f, "Default metallic mismatch");
        require((pack.images[0].pixels[0] & 0xffU) == 32U, "Red channel scalar height mismatch");
    }

    // B) Zero amplitude (0.0f) with max_displacement = 0.0f and no height image
    {
        const auto sidecar =
            build_bwsfm1(mesh_hash, 0.0f, 0.0f, 0.0f, no_surface_image, no_surface_image, 1U);
        std::istringstream iss(sidecar);
        const auto pack = read_surface_materials(iss, "bushwacker:close", mesh_hash, materials);
        require(pack.records[0]->height_amplitude == 0.0f, "Zero height_amplitude mismatch");
        require(pack.records[0]->max_displacement == 0.0f, "Zero max_displacement mismatch");
    }

    // C) Positive amplitude (+4.0f) with max_displacement = 4.0f
    {
        const auto sidecar = build_bwsfm1(mesh_hash, 4.0f, 4.0f, 0.5f);
        std::istringstream iss(sidecar);
        const auto pack = read_surface_materials(iss, "bushwacker:close", mesh_hash, materials);
        require(pack.records[0]->height_amplitude == 4.0f, "Positive height_amplitude mismatch");
        require(pack.records[0]->max_displacement == 4.0f, "max_displacement mismatch");
    }

    // D) Height reference tests: 0.0f, 0.5f, 1.0f all valid
    for (float ref : {0.0f, 0.5f, 1.0f}) {
        const auto sidecar = build_bwsfm1(mesh_hash, 2.0f, 2.0f, ref);
        std::istringstream iss(sidecar);
        const auto pack = read_surface_materials(iss, "bushwacker:close", mesh_hash, materials);
        require(pack.records[0]->height_reference == ref, "height_reference value mismatch");
    }

    // 3. Invariant check: max_displacement >= std::abs(height_amplitude)
    // CRITICAL: Negative amplitude (-4.0f) with max_displacement = 3.5f MUST be
    // rejected! (This tests the exact condition targeted by the production
    // negative control!)
    {
        const auto invalid_neg = build_bwsfm1(mesh_hash, -4.0f, 3.5f, 0.5f);
        require(rejected_surface_materials(invalid_neg, "bushwacker:close", mesh_hash, materials),
                "Invariant max_displacement >= abs(height_amplitude) violated for "
                "negative amplitude but accepted");
    }

    // Positive amplitude (+4.0f) with max_displacement = 3.5f MUST be rejected
    {
        const auto invalid_pos = build_bwsfm1(mesh_hash, 4.0f, 3.5f, 0.5f);
        require(rejected_surface_materials(invalid_pos, "bushwacker:close", mesh_hash, materials),
                "Invariant max_displacement >= abs(height_amplitude) violated for "
                "positive amplitude but accepted");
    }

    // 4. Fail-closed rejection tests on corrupted sidecars
    const auto valid_sidecar = build_bwsfm1(mesh_hash, -4.0f, 4.0f, 0.5f);

    // Truncated header (e.g. cut off at byte 16)
    require(rejected_surface_materials(valid_sidecar.substr(0, 16), "bushwacker:close", mesh_hash,
                                       materials),
            "Truncated 16-byte header was accepted");

    // Truncated at every byte offset
    for (std::size_t cut = 0; cut < valid_sidecar.size(); ++cut) {
        require(rejected_surface_materials(valid_sidecar.substr(0, cut), "bushwacker:close",
                                           mesh_hash, materials),
                "Truncated surface materials sidecar was accepted");
    }

    // Trailing bytes rejected
    require(rejected_surface_materials(valid_sidecar + "trailing", "bushwacker:close", mesh_hash,
                                       materials),
            "Trailing bytes in sidecar was accepted");

    // Unsorted records rejected: swap record names
    {
        auto unsorted = valid_sidecar;
        // Locate "armor_hull" and replace with "zebra" so that record 0 > record 1
        auto pos = unsorted.find("armor_hull");
        require(pos != std::string::npos, "armor_hull not found");
        unsorted.replace(pos, 10, "zebra_hull");
        require(rejected_surface_materials(unsorted, "bushwacker:close", mesh_hash, materials),
                "Unsorted records table was accepted");
    }

    // Bad FNV-1a64 hash on image pixels rejected
    {
        auto bad_hash = valid_sidecar;
        bad_hash.back() ^= 0xff; // corrupt pixel
        require(rejected_surface_materials(bad_hash, "bushwacker:close", mesh_hash, materials),
                "Corrupted pixel with bad FNV-1a64 hash was accepted");
    }

    // Mismatched mesh_uv_hash rejected
    require(
        rejected_surface_materials(valid_sidecar, "bushwacker:close", mesh_hash ^ 1ULL, materials),
        "Mismatched mesh_uv_hash was accepted");

    // Mismatched asset_id rejected
    require(rejected_surface_materials(valid_sidecar, "wrong:asset", mesh_hash, materials),
            "Mismatched asset_id was accepted");

    // Swapped batch materials rejected
    require(rejected_surface_materials(valid_sidecar, "bushwacker:close", mesh_hash,
                                       {"canopy_glass", "armor_hull"}),
            "Swapped batch materials was accepted");
}

void check_world_normals_lru_and_precedence() {
    using namespace mw34::materials;

    // 1. MW34SNA1 authored catalog tests in private invocation directory
    const auto temp_dir = make_private_test_dir("mw34_test_catalogs");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{temp_dir};

    // Test directory traversal rejection in MW34SNA1
    {
        const auto sna1_path = temp_dir / "bad_sna1.txt";
        {
            std::ofstream f(sna1_path, std::ios::binary);
            f << "MW34SNA1\n../outside_page.bin\n";
        }
        bool rejected = false;
        try {
            surface_normal_catalog_paths(sna1_path);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "MW34SNA1 path with directory traversal was accepted");
    }

    // 2. MW34SNP1 WorldNormalCache & 512 MiB LRU eviction tests
    auto make_page = [](std::uint64_t hash_base) {
        std::string stream("BWSNR2\0\0", 8);
        write_u32(stream, 1U); // 1 surface key
        write_u32(stream, 1U); // width
        write_u32(stream, 1U); // height
        write_u32(stream, 1U); // format RGB565
        write_u64(stream, hash_base);
        std::string mat = "Armor";
        mat.resize(32, '\0');
        stream.append(mat);

        // BWNRM2 payload: 1 batch, 1 image
        stream.append("BWNRM2\0\0", 8);
        write_u32(stream, 1U);
        write_u32(stream, 1U);
        write_u32(stream, 0U);
        stream.append(mat);
        write_u32(stream, 1U);
        write_u32(stream, 1U);
        const unsigned char px[]{128, 128, 255, 255};
        write_u64(stream, normal_hash(px, 4));
        stream.append(reinterpret_cast<const char *>(px), 4);
        return stream;
    };

    const auto p0_path = temp_dir / "page-0.bin";
    const auto p1_path = temp_dir / "page-1.bin";
    const auto p2_path = temp_dir / "page-2.bin";

    {
        std::ofstream(p0_path, std::ios::binary) << make_page(0x1000ULL);
        std::ofstream(p1_path, std::ios::binary) << make_page(0x2000ULL);
        std::ofstream(p2_path, std::ios::binary) << make_page(0x3000ULL);
    }

    const auto manifest_path = temp_dir / "world.txt";
    {
        std::ofstream f(manifest_path, std::ios::binary);
        f << "MW34SNP1\npage-0.bin\npage-1.bin\npage-2.bin\n";
    }

    // Test 16 MiB compressed page limit rejection
    {
        const auto huge_page = temp_dir / "huge_page.bin";
        {
            std::ofstream f(huge_page, std::ios::binary);
            f.seekp(16ULL * 1024 * 1024 + 1);
            f.write("X", 1);
        }
        const auto bad_manifest = temp_dir / "bad_world.txt";
        {
            std::ofstream f(bad_manifest, std::ios::binary);
            f << "MW34SNP1\nhuge_page.bin\n";
        }
        WorldNormalCache cache;
        bool rejected = false;
        try {
            cache.load_world_manifest(bad_manifest);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "Page exceeding 16 MiB limit was accepted");
    }

    // Test WP01-R01: Public lazy lookup without prior load_page call
    {
        WorldNormalCache cache;
        cache.load_world_manifest(manifest_path);
        require(cache.pages.size() == 3, "Manifest pages count mismatch");
        SurfaceKey key0{1, 1, 1, 0x1000ULL};
        require(!cache.pages[0].loaded, "Page 0 should not be loaded before first lookup");

        // First public surface_normal_index call MUST lazily load the page
        const auto lazy_index = cache.surface_normal_index(key0);
        require(lazy_index >= 0, "Lazy lookup returned negative index on valid world page");
        require(cache.pages[0].loaded,
                "First public surface_normal_index call failed to load page");
        require(cache.world_bytes == 4, "Resident byte accounting mismatch after lazy load");
        require(cache.textures.at(static_cast<std::size_t>(lazy_index)).pixels32.size() == 1,
                "Lazy lookup did not decode normal pixels into texture record");

        // Verify bounded LRU eviction and reload through the public API
        // Set budget to 8 bytes: fits 2 pages of 4 bytes each
        const std::uint64_t budget = 8;
        SurfaceKey key1{1, 1, 1, 0x2000ULL};
        const auto idx1 = cache.surface_normal_index(key1);
        require(idx1 >= 0 && cache.pages[1].loaded, "Page 1 lazy load failed");

        // Load page 2 with budget = 8 bytes -> evicts least recently used (page 0)
        cache.load_page(2, budget);
        require(cache.pages[2].loaded, "Page 2 load failed");
        require(!cache.pages[0].loaded, "Page 0 should have been evicted under 8-byte budget");
        require(cache.textures.at(static_cast<std::size_t>(lazy_index)).pixels32.empty(),
                "Evicted page 0 pixels were not freed");

        // Public lookup on key0 must transparently reload page 0 and return stable
        // index
        const auto reloaded_index = cache.surface_normal_index(key0);
        require(reloaded_index == lazy_index, "Reloaded normal index changed after eviction");
        require(cache.pages[0].loaded, "Public lookup did not reload evicted page 0");
        require(!cache.textures.at(static_cast<std::size_t>(reloaded_index)).pixels32.empty(),
                "Reloaded page 0 pixels were not restored");
    }

    // Test WP01-R01: Authored-over-world precedence via public API
    {
        WorldNormalCache cache;
        SurfaceKey authored_key{1, 1, 1, 0x1000ULL}; // matches key in page 0
        cache.register_authored(authored_key, 42);   // authored binding to index 42

        SurfaceKey excluded_key{1, 1, 1, 0x2000ULL}; // matches key in page 1
        cache.register_authored(excluded_key,
                                -1); // explicit authored exclusion (no_normal)

        cache.load_world_manifest(manifest_path);
        // Public lookup must preserve authored precedence without loading pages
        require(cache.surface_normal_index(authored_key) == 42,
                "Authored binding was not preserved by public surface_normal_index");
        require(cache.surface_normal_index(excluded_key) == -1,
                "Explicit authored exclusion was not preserved by public "
                "surface_normal_index");
        require(!cache.pages[0].loaded, "Page 0 loaded despite authored binding override");
        require(!cache.pages[1].loaded, "Page 1 loaded despite authored exclusion override");

        // Third unauthored key in page 2 DOES lazily load page 2
        SurfaceKey world_key{1, 1, 1, 0x3000ULL};
        const auto world_idx = cache.surface_normal_index(world_key);
        require(world_idx >= 0 && cache.pages[2].loaded,
                "Unauthored world key failed to lazily load page 2");
    }

    // Test WP01-R03: Typed configuration ownership (independent per-instance &
    // per-call choices)
    {
        SurfaceKey key0{1, 1, 1, 0x1000ULL};

        // Typed ON instance:
        WorldNormalCache cache_on(true);
        cache_on.load_world_manifest(manifest_path);
        require(cache_on.surface_normal_index(key0) >= 0, "Typed ON cache returned -1");

        // Typed OFF instance:
        WorldNormalCache cache_off(false);
        cache_off.load_world_manifest(manifest_path);
        require(cache_off.surface_normal_index(key0) == -1,
                "Typed OFF cache returned normal index");
        require(!cache_off.pages[0].loaded, "Typed OFF cache loaded page unexpectedly");

        // Per-call typed parameter override:
        require(cache_on.surface_normal_index(key0, false) == -1,
                "Per-call disabled lookup returned normal index");
        require(cache_on.surface_normal_index(key0, true) >= 0,
                "Per-call enabled lookup failed to return normal index");
    }
}

#include "prepared_environment_checks.inc"

void checks() {
  check_environment_packs();
  std::istringstream tex_input(textures());
  const auto tex = platform::prepared::read_textures(tex_input);
  require(tex.images.size() == 1 &&
              tex.images[0].pixels == std::vector<uint16_t>{0xf800, 0x07e0},
          "RGB565 material pixels changed");
  const auto read_normals = [&](std::istream &input) {
    return platform::prepared::read_normals(input, tex);
  };
  std::istringstream normal_input(prepared_fixture::normals(0x40ff8080));
  const auto normals = read_normals(normal_input);
  require(normals.images.size() == 1 &&
              normals.images[0].pixels[0] == 0x40ff8080,
          "Normal bytes or authored alpha changed");
  std::istringstream absent_input(prepared_fixture::normals(0, true));
  require(read_normals(absent_input).bindings[0] ==
              platform::prepared::no_normal,
          "Explicit absent normal binding was lost");
  const auto normal_bytes = prepared_fixture::normals();
  rejects(normal_bytes.substr(0, normal_bytes.size() - 1), read_normals);
  rejects(normal_bytes + "extra", read_normals);
  for (size_t offset : {size_t(6), size_t(8), size_t(16), size_t(20),
                        size_t(52), size_t(60), size_t(68)}) {
    auto bad = normal_bytes;
    bad[offset] ^= char(0x7f);
    rejects(bad, read_normals);
  }
    for (unsigned version = 1; version <= 6; ++version) {
        const auto data = mesh(version);
        std::istringstream input(data);
        const auto parsed = platform::prepared::read_mesh(input);
        require(parsed.version == version && parsed.batches.size() == 1 &&
                    parsed.batches[0].vertices.size() == 3,
                "Prepared mesh layout changed");
        platform::prepared::validate_bindings(parsed, tex);
        rejects(data.substr(0, data.size() - 1), platform::prepared::read_mesh);
        rejects(data + "extra", platform::prepared::read_mesh);
    }
    auto bad_mesh = mesh(2);
    bad_mesh[8] = 0;
    rejects(bad_mesh, platform::prepared::read_mesh);
    bad_mesh = mesh(2);
    bad_mesh.replace(bad_mesh.size() - 4, 4, std::string("\0\0\x80\x7f", 4));
    rejects(bad_mesh, platform::prepared::read_mesh);
    auto bad_texture = textures();
    bad_texture[16] = 1;
    rejects(bad_texture, platform::prepared::read_textures);
    bad_texture = textures();
    bad_texture[52] = 0;
    rejects(bad_texture, platform::prepared::read_textures);
    rejects(textures() + "extra", platform::prepared::read_textures);
    rejects(textures().substr(0, textures().size() - 1), platform::prepared::read_textures);
    auto *root = zcls::object3d_create();
    require(root != nullptr, "Could not create an authored model tree");
    auto identity = std::make_shared<zcls::SourceAsset>();
    identity->name = "mech_authored.flt";
    root->source_asset = identity;
    root->source_asset_root = true;
    auto *copy = zcls::tree_copy(root, 0, 0);
    require(copy && copy->source_asset == root->source_asset && copy->source_asset_root,
            "Model-tree copy lost source identity");
    zcls::tree_destroy(copy);
    zcls::tree_destroy(root);
    const auto saved_fog = zmod::fog();
    const auto saved_calc = zmod::fog_calc();
    zmod::fog() = {};
    auto &fog = zmod::fog();
    fog.on = 1;
    fog.far_range = 10;
    fog.far_sq = 100;
    fog.inv_range = .1f;
    fog.alt_high = 10;
    fog.inv_alt = .1f;
    zmod::fog_calc().camera_to_world[1][1] = 1;
    const float vertices[][3] = {{0, 0, 0}, {0, -1, 100}, {0, 11, 100}, {0, 5, 100}};
    const float expected[] = {0, 1, 0, .5f};
    zmod::fog_calc().verts = vertices;
    require(zmod::fog_vertices(4) == 1, "Native vertex fog did not report visible fog");
    for (unsigned i = 0; i < 4; ++i)
        require(zmod::fog_at(vertices[i]) == expected[i] &&
                    zmod::fog_calc().factor[i] == expected[i],
                "Prepared/native fog sampling changed near/far or altitude semantics");
    zmod::fog() = saved_fog;
    zmod::fog_calc() = saved_calc;
}

// An invalid binding fails inside the synchronous producer callback. Both its
// visual scope and the native matrix push must unwind before another frame.
void check_producer_unwind() {
    struct RejectingRenderer : platform::Render3D {
        bool ended = false;
        bool wants_model_context() const override { return true; }
        void begin_model_draw(const platform::ModelDrawContext &) override {
            throw std::runtime_error("authored binding rejection");
        }
        void end_model_draw() noexcept override { ended = true; }
        int32_t create_texture(int, int, const uint8_t *, uint8_t) override { return 0; }
        void release_texture(int32_t) override {}
        void update_texture(int32_t, int, int, const uint8_t *) override {}
        void set_viewport(int, int, int, int) override {}
        void clear(bool, uint8_t, uint8_t, uint8_t) override {}
        void begin_scene() override {}
        void end_scene() override {}
        void draw(platform::Primitive, const platform::TLVertex *, int,
                  const platform::DrawState &) override {}
        void set_fog_color(uint8_t, uint8_t, uint8_t) override {}
        void present() override {}
    } renderer;
    const auto saved_stack = zmth::matrix_stack();
    const auto saved_view = zmth::view();
    const auto saved_draw = zmod::draw_state();
    auto *saved_renderer = zvid::render3d();
    zmth::reset_stack();
    zmth::Mat34 root{};
    zmth::push(&root);
    zmth::load_identity();
    zmth::view().view = root;
    const auto depth = zmth::matrix_stack().top;
    zcls::Node node{};
    zmod::Model model{};
    node.model = &model;
    zvid::set_render3d(&renderer);
    bool rejected = false;
    try {
        zmod::draw_model(&node, 0);
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()) == "authored binding rejection";
    }
    const bool balanced =
        zmth::matrix_stack().top == depth && zmth::matrix_stack().slot[depth] == &root;
    zvid::set_render3d(saved_renderer);
    zmth::matrix_stack() = saved_stack;
    zmth::view() = saved_view;
    zmod::draw_state() = saved_draw;
    require(rejected && renderer.ended && balanced,
            "Model rejection did not unwind the native matrix/visual scopes");
}
} // namespace

int main(int argc, char **argv) {
    try {
      if (argc >= 3 && std::string(argv[1]) == "--check-environment") {
        std::vector<std::string> roots;
        for (int i = 2; i < argc; ++i)
          roots.emplace_back(argv[i]);
        const auto admitted = platform::prepared::load_environment_packs(roots);
        std::printf(
            "Environment selection: %zu regions, %zu banks, identity=%s\n",
            admitted.regions.size(), admitted.banks.size(),
            admitted.identity.c_str());
      } else if ((argc == 4 || argc == 5) &&
                 std::string(argv[1]) == "--check") {
        std::ifstream mesh_input(std::filesystem::u8path(argv[2]),
                                 std::ios::binary),
            texture_input(std::filesystem::u8path(argv[3]), std::ios::binary);
        if (!mesh_input || !texture_input)
          throw std::runtime_error(
              "Cannot open the selected prepared components");
        const auto geometry = platform::prepared::read_mesh(mesh_input);
        const auto materials = platform::prepared::read_textures(texture_input);
        platform::prepared::validate_bindings(geometry, materials);
        if (argc == 5) {
          std::ifstream normal_input(std::filesystem::u8path(argv[4]),
                                     std::ios::binary);
          if (!normal_input)
            throw std::runtime_error("Cannot open selected normal component");
          const auto normals =
              platform::prepared::read_normals(normal_input, materials);
          std::printf("BWNRM: %zu images; ", normals.images.size());
        }
        std::printf("BWMESH%u: %zu batches; BWTEX1: %zu images\n",
                    geometry.version, geometry.batches.size(),
                    materials.images.size());
      } else if (argc == 1) {
        checks();
        check_normal_codec();
        check_bwnrm_formats();
        check_surface_materials_contract();
        check_world_normals_lru_and_precedence();
        check_producer_unwind();
        std::printf(
            "All prepared visual, normal, and surface material contract "
            "checks passed successfully.\n");
      } else
        throw std::runtime_error(
            "usage: mw3prepared_checks [--check MESH TEXTURES [NORMALS]]");
      return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
