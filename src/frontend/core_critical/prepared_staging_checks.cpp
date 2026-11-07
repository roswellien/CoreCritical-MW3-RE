#include "../../../tests/prepared_visual_fixture.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zmodel/gmod_draw.h"
#include "prepared_draw_cache.hpp"
#include "prepared_models.hpp"
#include "renderer/normal_codec.hpp"
#include "renderer/surface_materials.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void same(const cc_mw3_re::PreparedSubmission &a, const cc_mw3_re::PreparedSubmission &b) {
    require(a.vertices == b.vertices && a.clipped_triangles == b.clipped_triangles &&
                a.draw.count == b.draw.count && a.draw.sampler == b.draw.sampler &&
                !std::memcmp(a.draw.push.mode, b.draw.push.mode, sizeof a.draw.push.mode),
            "Cached prepared bytes or draw count changed");
    require(!std::memcmp(a.draw.push.fog, b.draw.push.fog, sizeof a.draw.push.fog) &&
                !std::memcmp(a.draw.push.target, b.draw.push.target, sizeof a.draw.push.target),
            "Cached fog or target changed");
    require(bool(a.draw.model) == bool(b.draw.model), "Cached GPU admission changed");
    if (a.draw.model) {
        require(a.draw.model->geometry == b.draw.model->geometry &&
                    a.draw.model->camera == b.draw.model->camera &&
                    a.draw.model->projection == b.draw.model->projection &&
                    a.draw.model->sun_world == b.draw.model->sun_world &&
                    a.draw.model->sun_normal == b.draw.model->sun_normal,
                "Cached model instance changed");
    }
    require(bool(a.draw.sun_tl) == bool(b.draw.sun_tl) &&
                bool(a.draw.lit_normals) == bool(b.draw.lit_normals),
            "Cached clipping metadata changed");
    if (a.draw.sun_tl)
        require(a.draw.sun_tl->camera_world == b.draw.sun_tl->camera_world &&
                    a.draw.sun_tl->projection == b.draw.sun_tl->projection &&
                    *a.draw.lit_normals == *b.draw.lit_normals,
                "Cached clipped normals changed");
}
void checks() {
    namespace cc = cc_mw3_re;
    platform::prepared::Batch batch;
    batch.color = 0xffe08040;
    batch.vertices = {
        {-1, -1, 2, 0, 0, 1, 0, 0}, {1, -1, 2, 0, 0, 1, 1, 0}, {0, 1, 2, 0, 0, 1, .5f, 1}};
    cc::PreparedBatchDraw source;
    source.batch = &batch;
    source.camera = source.world = mw34::donor::identity();
    auto geometry = std::make_shared<mw34::vkscene::ModelGeometry>();
    platform::ModelDrawContext context;
    int actor_a = 0, actor_b = 0;
    context.node = &actor_a;
    context.near_z = context.clip_near = .1f;
    context.far_z = 100;
    context.fx_near = context.fy_near = 320;
    context.cx = context.cy = 32;
    std::array<int, 4> viewport{0, 0, 64, 64};
    unsigned width = 64, height = 64;
    bool lighting = true;
    zmod::fog() = {};
    zmod::fog().on = 1;
    zmod::fog().near_range = 1;
    zmod::fog().near_sq = 1;
    zmod::fog().far_range = 10;
    zmod::fog().far_sq = 100;
    zmod::fog().inv_range = 1.f / 9;
    zmod::fog().alt_low = 5;
    zmod::fog().alt_high = 10;
    zmod::fog().inv_alt = .2f;
    auto &camera = zmod::fog_calc().camera_to_world;
    std::memset(camera, 0, sizeof camera);
    camera[0][0] = camera[1][1] = camera[2][2] = 1;
    cc::PreparedDrawCache cache;
    cc::PreparedDrawCpuStatistics profile;
    const auto reference = [&] {
        return cc::prepare_draw(source, context, viewport, width, height, geometry, lighting);
    };
    const auto get = [&] {
        return cache.get(source, context, viewport, width, height, geometry, lighting, &profile);
    };
    if (!cc::PreparedDrawCache::supports_control_state()) {
        same(*get(), reference());
        require(cache.statistics().bypasses == 1, "Unsupported FP controls entered cache");
        return;
    }
    const auto first = get();
    same(*first, reference());
    const auto processed = profile.vertices;
    require(get() == first && cache.statistics().hits == 1 && profile.vertices == processed,
            "Unchanged prepared input was recomputed");
    const auto miss = [&] {
        const auto before = cache.statistics().misses;
        same(*get(), reference());
        require(cache.statistics().misses == before + 1,
                "Changed prepared input reused stale data");
    };
    source.camera[3] += .25;
    miss();
    source.world[7] += .5;
    miss();
    for (auto member : {&platform::ModelDrawContext::near_z, &platform::ModelDrawContext::clip_near,
                        &platform::ModelDrawContext::far_z, &platform::ModelDrawContext::fx_near,
                        &platform::ModelDrawContext::fy_near, &platform::ModelDrawContext::cx,
                        &platform::ModelDrawContext::cy}) {
        context.*member += .01f;
        miss();
    }
    ++viewport[0];
    miss();
    ++width;
    miss();
    ++height;
    miss();
    lighting = false;
    miss();
    zmod::fog().color[1] = .5f;
    miss();
    zmod::fog().inv_range = .5f;
    miss();
    camera[3][1] = 2;
    miss();
    zmod::fog().on = 0;
    miss();
    geometry = std::make_shared<mw34::vkscene::ModelGeometry>();
    miss();
    context.node = &actor_b;
    miss();
    const int rounding = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    miss();
    std::fesetround(rounding);
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    const auto rounded = get();
    require(get() == rounded, "Restored rounding did not stabilize the cache fixture");
    const auto controls = _mm_getcsr();
    _mm_setcsr(controls ^ 0x8000U);
    miss();
    _mm_setcsr(controls);
    const auto unflushed = get();
    require(get() == unflushed, "Restored FTZ did not stabilize the cache fixture");
    _mm_setcsr(controls ^ 0x40U);
    miss();
    _mm_setcsr(controls);
#endif
    const auto bypasses = cache.statistics().bypasses;
    source.deformed = batch.vertices;
    same(*get(), reference());
    source.deformed[0][0] += .125f;
    same(*get(), reference());
    require(cache.statistics().bypasses == bypasses + 2,
            "Mutable deformation entered the static cache");
    source.deformed.clear();
    lighting = true;
    source.camera[8] = 1.95;
    miss();
    require(!get()->draw.model && get()->clipped_triangles > 0,
            "Clipped cache fixture did not clip");
    auto owned =
        cache.get(source, context, viewport, width, height, geometry, lighting, nullptr, false);
    same(*owned, reference());
    cc::PreparedDrawCache bounded(1024 * 1024, 1);
    const auto evicted = bounded.get(source, context, viewport, width, height, geometry, lighting);
    context.node = &actor_a;
    bounded.get(source, context, viewport, width, height, geometry, lighting);
    require(bounded.statistics().entries == 1 && bounded.statistics().bytes <= 1024 * 1024,
            "Prepared cache exceeded its bounds");
    same(*evicted, reference());
    cc::PreparedDrawCache tiny(1, 1);
    tiny.get(source, context, viewport, width, height, geometry, lighting);
    require(tiny.statistics().entries == 0 && tiny.statistics().bytes == 0,
            "Oversized staging entered the prepared cache");
    auto large_batch = batch;
    for (unsigned copy = 1; copy < 128; ++copy)
        large_batch.vertices.insert(large_batch.vertices.end(), batch.vertices.begin(),
                                    batch.vertices.end());
    source.batch = &large_batch;
    const auto clipped = reference();
    require(clipped.draw.lit_normals && !clipped.draw.lit_normals->empty(),
            "Clipped budget fixture has no normal stream");
    const auto payload = clipped.vertices.capacity() + sizeof(cc::PreparedSubmission) +
                         sizeof(*clipped.draw.sun_tl) + sizeof(*clipped.draw.lit_normals) +
                         clipped.draw.lit_normals->capacity() * sizeof(std::array<float, 3>);
    cc::PreparedDrawCache clipped_budget(payload - 1);
    clipped_budget.get(source, context, viewport, width, height, geometry, lighting);
    require(clipped_budget.statistics().entries == 0 && clipped_budget.statistics().bytes == 0,
            "Clipped normal payload escaped the prepared cache byte budget");
    source.batch = &batch;
    const auto valid_far = context.far_z;
    context.far_z = context.near_z;
    bool rejected = false;
    try {
        get();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Cache hid invalid clip planes");
    context.far_z = valid_far;
    same(*get(), reference());
    require(!first->vertices.empty() && !owned->vertices.empty(),
            "Eviction invalidated an owned result");
}

static std::filesystem::path make_private_staging_dir(const char *prefix) {
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

static void write_u32(std::string &out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffU));
}
static void write_u64(std::string &out, std::uint64_t value) {
    write_u32(out, static_cast<std::uint32_t>(value & 0xffffffffU));
    write_u32(out, static_cast<std::uint32_t>((value >> 32) & 0xffffffffU));
}
static void write_f32(std::string &out, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    write_u32(out, bits);
}
static void write_str(std::string &out, const std::string &value) {
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out.append(value);
}

static std::string build_single_batch_bwsfm1(std::uint64_t mesh_hash, const std::string &asset_id,
                                             const std::string &material_name,
                                             float height_amp = -4.0f, float max_disp = 4.0f,
                                             float height_ref = 0.5f, float roughness = 0.7f,
                                             float reflectivity = 0.04f, float metallic = 0.0f) {
    using namespace mw34::materials;
    const std::array<unsigned char, 16> h_pixels{
        32, 0, 0, 255, 96, 0, 0, 255, 160, 0, 0, 255, 224, 0, 0, 255,
    };
    std::string out("BWSFM1\0\0", 8);
    write_u32(out, 1U);        // version
    write_u32(out, 1U);        // batch_count = 1
    write_u32(out, 1U);        // record_count = 1
    write_u32(out, 1U);        // image_count = 1
    write_u64(out, mesh_hash); // mesh_uv_hash

    // Batch 0 -> record 0
    write_u32(out, 0U);

    // Record 0
    write_str(out, asset_id);
    write_str(out, material_name);
    write_u32(out, 0U);               // height_image = 0
    write_u32(out, no_surface_image); // thick_image = none
    write_f32(out, height_ref);
    write_f32(out, height_amp);
    write_f32(out, max_disp);
    write_u32(out, 1U);   // tess_cap
    write_f32(out, 1.0f); // uv_scale[0]
    write_f32(out, 1.0f); // uv_scale[1]
    write_f32(out, 0.0f); // uv_offset[0]
    write_f32(out, 0.0f); // uv_offset[1]
    write_u32(out, surface_address_wrap);
    write_u32(out, surface_address_wrap);
    write_f32(out, 0.0f); // atlas_rect
    write_f32(out, 0.0f);
    write_f32(out, 1.0f);
    write_f32(out, 1.0f);
    write_f32(out, 0.0f); // sss_participation
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f);
    write_f32(out, 0.0f); // sss_radius
    write_f32(out, 0.0f); // thickness_scale
    write_f32(out, roughness);
    write_f32(out, reflectivity);
    write_f32(out, metallic);

    // Image 0
    write_str(out, "h_img");
    write_u32(out, surface_role_height);
    write_u32(out, 2U);
    write_u32(out, 2U);
    write_u64(out, normal_hash(h_pixels.data(), h_pixels.size()));
    out.append(reinterpret_cast<const char *>(h_pixels.data()), h_pixels.size());

    return out;
}

void check_prepared_models_surface_materials() {
    const auto temp_dir = make_private_staging_dir("mw34_pm_materials");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{temp_dir};

    const auto mesh_path = temp_dir / "mesh.bin";
    const auto tex_path = temp_dir / "textures.bin";
    const auto mat_path = temp_dir / "materials.bin";

    {
        std::ofstream(mesh_path, std::ios::binary) << prepared_fixture::mesh(2);
        std::ofstream(tex_path, std::ios::binary) << prepared_fixture::textures();
    }

    std::uint64_t mesh_uv_hash = 0;
    {
        std::ifstream mf(mesh_path, std::ios::binary);
        const auto m = platform::prepared::read_mesh(mf);
        mesh_uv_hash = mw34::materials::compute_mesh_uv_hash(m.batches, 0, m.batches.size());
    }

    {
        std::ofstream f(mat_path, std::ios::binary);
        f << build_single_batch_bwsfm1(mesh_uv_hash, "authored:close", "authored", -4.0f, 4.0f,
                                       0.5f);
    }

    std::unique_ptr<zcls::Node, int (*)(zcls::Node *)> root(zcls::object3d_create(),
                                                            zcls::tree_destroy);
    auto *control = zcls::object3d_create(), *panel = zcls::object3d_create();
    require(root && control && panel && zcls::object3d_add_child(root.get(), control) == 0 &&
                zcls::object3d_add_child(control, panel) == 0,
            "Node hierarchy setup failed");
    zcls::node_set_name(root.get(), "actor");
    zcls::node_set_name(control, "control");
    zcls::node_set_name(panel, "panel");
    auto asset = std::make_shared<zcls::SourceAsset>();
    asset->name = "mech_authored.flt";
    asset->nodes = {{"actor", 1}, {"control", 1}, {"panel", 1}};
    root->source_asset = control->source_asset = panel->source_asset = asset;
    root->source_asset_root = true;
    zmod::Model model{};
    const std::uint32_t count = 3;
    std::memcpy(model.header + 0x14, &count, sizeof count);
    panel->model = &model;
    panel->source_model_vertices = count;
    struct ModelPointer {
        zcls::Node *node;
        ~ModelPointer() { node->model = nullptr; }
    } model_pointer{panel};

    std::array<float, 12> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    platform::ModelDrawContext context{};
    context.node = panel;
    context.ordinary = true;
    context.source_vertices = count;
    context.source_polygons = 1;
    context.model_to_camera = context.model_to_world = matrix.data();
    context.opacity = 1;

    sdlfe::CoreCriticalAssets options;
    options.models.push_back({"authored", "0", mesh_path.string(), tex_path.string()});

    auto mat_binding = [](const std::string &c, const std::string &l,
                          const std::filesystem::path &p) {
        return std::vector<cc_mw3_re::PreparedMaterialBinding>{{c, l, p.string()}};
    };

    // 1. Positive test: PreparedModels loads valid BWSFM1 and exposes it on
    // PreparedBatchDraw
    {
        cc_mw3_re::PreparedModels models(options, mat_binding("authored", "0", mat_path));
        require(models.has_binding(context), "PreparedModels missing binding for authored node");
        const auto draw = models.draw(context);
        require(draw.batches.size() == 1, "Draw batches count mismatch");
        require(draw.batches[0].surface_material != nullptr,
                "surface_material pointer was null on positive draw");
        require(draw.batches[0].surface_material->height_amplitude == -4.0f,
                "height_amplitude mismatch");
        require(draw.batches[0].surface_material->max_displacement == 4.0f,
                "max_displacement mismatch");
        require(draw.batches[0].surface_material->roughness == 0.7f, "Default roughness mismatch");
        require(draw.batches[0].surface_material->reflectivity == 0.04f,
                "Default reflectivity mismatch");
        require(draw.batches[0].surface_material->metallic == 0.0f, "Default metallic mismatch");
        require(draw.batches[0].surface_material->material_name == "authored",
                "material_name mismatch");
    }

    // 2. Stock / No-Sidecar Control: surface_material must be nullptr
    {
        cc_mw3_re::PreparedModels stock_models(options);
        const auto stock_draw = stock_models.draw(context);
        require(stock_draw.batches.size() == 1, "Stock draw batches count mismatch");
        require(stock_draw.batches[0].surface_material == nullptr,
                "surface_material must be null for stock model without sidecar");
    }

    // 3. Wrong Asset ID Rejection
    {
        const auto bad_asset_path = temp_dir / "bad_asset.bin";
        {
            std::ofstream f(bad_asset_path, std::ios::binary);
            f << build_single_batch_bwsfm1(mesh_uv_hash, "wrong_chassis:close", "authored");
        }
        bool rejected = false;
        try {
            cc_mw3_re::PreparedModels bad_models(options,
                                                 mat_binding("authored", "0", bad_asset_path));
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "PreparedModels accepted BWSFM1 with mismatched asset ID");
    }

    // 4. Wrong Mesh UV Hash Rejection
    {
        const auto bad_uv_path = temp_dir / "bad_uv.bin";
        {
            std::ofstream f(bad_uv_path, std::ios::binary);
            f << build_single_batch_bwsfm1(mesh_uv_hash ^ 0xbadULL, "authored:close", "authored");
        }
        bool rejected = false;
        try {
            cc_mw3_re::PreparedModels bad_uv_models(options,
                                                    mat_binding("authored", "0", bad_uv_path));
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "PreparedModels accepted BWSFM1 with mismatched mesh UV hash");
    }

    // 5. Wrong Material Name Rejection
    {
        const auto bad_mat_path = temp_dir / "bad_mat.bin";
        {
            std::ofstream f(bad_mat_path, std::ios::binary);
            f << build_single_batch_bwsfm1(mesh_uv_hash, "authored:close", "WrongMat");
        }
        bool rejected = false;
        try {
            cc_mw3_re::PreparedModels bad_mat_models(options,
                                                     mat_binding("authored", "0", bad_mat_path));
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "PreparedModels accepted BWSFM1 with mismatched batch material name");
    }

    // 6. Unmatched Sidecar Selection Rejection
    {
        bool rejected = false;
        try {
            cc_mw3_re::PreparedModels unmatched_models(
                options, mat_binding("unmatched_chassis", "0", mat_path));
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "PreparedModels accepted unmatched chassis selection");
    }

    // 7. Independent Effect Enablement: metadata populated regardless of lighting
    // setting
    {
        sdlfe::CoreCriticalAssets off_options = options;
        off_options.lighting = "off";
        cc_mw3_re::PreparedModels off_models(off_options, mat_binding("authored", "0", mat_path));
        const auto off_draw = off_models.draw(context);
        require(off_draw.batches.size() == 1, "Off draw batches count mismatch");
        require(off_draw.batches[0].surface_material != nullptr,
                "surface_material metadata must be available when lighting is off");
    }
}
} // namespace

int main() {
    try {
        checks();
        check_prepared_models_surface_materials();
        std::puts("Prepared staging cache invalidation, bytes, ownership, "
                  "materials and budget checks passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Prepared staging: %s\n", error.what());
        return 1;
    }
}
