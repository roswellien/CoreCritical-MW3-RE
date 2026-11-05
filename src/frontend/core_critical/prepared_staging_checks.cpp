#include "prepared_draw_cache.hpp"
#include <cstdio>

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
} // namespace
int main() {
    try {
        checks();
        std::puts("Prepared staging cache invalidation, bytes, ownership and budget checks passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Prepared staging: %s\n", error.what());
        return 1;
    }
}
