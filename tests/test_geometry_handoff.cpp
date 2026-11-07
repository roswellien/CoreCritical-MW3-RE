#include "frontend/core_critical/renderer/geometry/resource.hpp"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_geometry.h"
#include "gamez/zvideo/zvid_ddd3d.h"

#include <cfenv>
#include <cstring>
#include <iostream>
#include <stdexcept>

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Renderer : platform::Render3D {
    cc::geometry::Mode mode = cc::geometry::Mode::partial;
    std::shared_ptr<const cc::geometry::Resource> submitted;
    unsigned legacy_draws{}, unsupported{};
    bool accept = true;
    bool prepared = false;
    cc::geometry::Submission submit_prepared_geometry(const platform::ModelDrawContext &context,
                                                      const cc::geometry::Fog &) override {
      if (!prepared) return cc::geometry::Submission::unsupported;
      require(context.source_vertices == 3 && context.source_polygons == 1,
              "Early prepared source work was not accounted for");
      return cc::geometry::Submission::submitted;
    }
    cc::geometry::Mode geometry_mode() const override { return mode; }
    cc::geometry::Submission submit_model_geometry(
        std::shared_ptr<const cc::geometry::Resource> source, const platform::ModelDrawContext &,
        const std::vector<cc::geometry::Material> &materials, const cc::geometry::Fog &) override {
        require(materials.size() == source->ranges.size(), "Material ownership missing");
        std::fesetround(FE_DOWNWARD);
        std::feraiseexcept(FE_INVALID);
        if (!accept)
            return cc::geometry::Submission::unsupported;
        submitted = std::move(source);
        return cc::geometry::Submission::submitted;
    }
    void note_cpu_model_geometry() override { ++unsupported; }
    int32_t create_texture(int, int, const uint8_t *, uint8_t) override { return 0; }
    void release_texture(int32_t) override {}
    void update_texture(int32_t, int, int, const uint8_t *) override {}
    void set_viewport(int, int, int, int) override {}
    void clear(bool, uint8_t, uint8_t, uint8_t) override {}
    void begin_scene() override {}
    void end_scene() override {}
    void draw(platform::Primitive, const platform::TLVertex *, int,
              const platform::DrawState &) override {
        ++legacy_draws;
    }
    void set_fog_color(uint8_t, uint8_t, uint8_t) override {}
    void present() override {}
};

int main() try {
    float positions[]{-1, -1, 3, 1, -1, 3, 0, 1, 3};
    uint32_t indices[]{0, 1, 2};
    zmod::Material material{};
    material.alpha = 255;
    material.r = 255;
    zmod::Polygon polygon{};
    polygon.flags = 3;
    polygon.vertex_indices = indices;
    polygon.material = &material;
    zmod::Model model{};
    model.vertices = positions;
    model.polygons = &polygon;
    auto set = [&](int offset, uint32_t value) { std::memcpy(model.header + offset, &value, 4); };
    set(8, 3);
    set(0x10, 1);
    set(0x14, 3);
    zcls::Node node{};
    node.model = &model;
    zmth::Mat34 base{{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
    zmth::matrix_stack() = {};
    zmth::matrix_stack().slot[0] = &base;
    zmth::matrix_stack().identity[0] = 1;
    zmth::view().view = base;
    auto &projection = zmth::projection();
    projection.near_z = 1;
    projection.far_z = 10;
    projection.fx_near = projection.fy_near = 16;
    projection.cx = projection.cy = 16;
    zmod::draw_state() = {};
    zmod::draw_state().clip.near_z = 1;
    Renderer renderer;
    zvid::set_render3d(&renderer);
    auto &buffers = zmod::draw_buffers();
    std::memset(buffers.cam, 0x5a, sizeof buffers.cam);
    std::memset(buffers.normals, 0x6b, sizeof buffers.normals);
    auto &work = zmod::clip2d();
    std::memset(work.tl, 0x7c, sizeof work.tl);
    const auto before = buffers;
    const auto before_work = work;
    renderer.prepared = true;
    require(zmod::draw_model(&node, 0x3f) == 0 && !model.render_geometry && !renderer.submitted &&
                renderer.legacy_draws == 0 &&
                !std::memcmp(before.cam, buffers.cam, sizeof buffers.cam) &&
                !std::memcmp(before.normals, buffers.normals, sizeof buffers.normals) &&
                !std::memcmp(before_work.tl, work.tl, sizeof work.tl),
            "Prepared early handoff still registered stock geometry or ran CPU producer loops");
    renderer.prepared = false;
    require(zmod::draw_model(&node, 0x3f) == 0, "Original model return changed");
    require(renderer.submitted && renderer.legacy_draws == 0,
            "Early owned submission was bypassed");
    require(!std::memcmp(before.cam, buffers.cam, sizeof buffers.cam) &&
                !std::memcmp(before.normals, buffers.normals, sizeof buffers.normals) &&
                !std::memcmp(before_work.tl, work.tl, sizeof work.tl),
            "CPU transform/normal/polygon output was still produced");
    require(zmth::matrix_stack().top == 0, "Original matrix stack did not unwind");
    const auto original = renderer.submitted;
    require(original->indices.size() == 3 && original->vertices[0].y == 1,
            "Corner ordering was not normalized");
    require(zmod::draw_model(&node, 0x3f) == 0 && renderer.submitted == original,
            "Unchanged source rebuilt immutable geometry");
    positions[0] = -2;
    model.invalidate_render_geometry();
    require(zmod::draw_model(&node, 0x3f) == 0 && renderer.submitted != original &&
                renderer.submitted->identity != original->identity && original->vertices[2].x == -1,
            "Changed source reused or overwrote a pending immutable resource");
    std::fesetround(FE_UPWARD);
    std::feclearexcept(FE_ALL_EXCEPT);
    std::feraiseexcept(FE_DIVBYZERO);
    zmth::Mat34 camera = base;
    zmth::matrix_stack().top = 1;
    zmth::matrix_stack().slot[1] = &camera;
    require(zmod::submit_geometry(node), "Direct early handoff rejected authored geometry");
    require(std::fegetround() == FE_UPWARD && std::fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO,
            "Rendering callback leaked FP environment");
    std::fesetround(FE_TONEAREST);
    zmth::matrix_stack().top = 0;
    material.alpha = 100;
    renderer.mode = cc::geometry::Mode::gpu;
    bool rejected = false;
    try {
        zmod::draw_model(&node, 0x3f);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected && renderer.unsupported == 1 && zmth::matrix_stack().top == 0,
            "Strict GPU mode silently used CPU geometry or failed to unwind");
    material.alpha = 255;
    renderer.mode = cc::geometry::Mode::partial;
    renderer.accept = false;
    require(!zmod::submit_geometry(node), "Unsupported backend was reported as submitted");
    bool bad_index = false;
    indices[0] = 999;
    try {
        cc::geometry::register_resource(positions, 3, nullptr, 0,
                                        {{3, indices, nullptr, nullptr, false, false}});
    } catch (const std::runtime_error &) {
        bad_index = true;
    }
    require(bad_index, "Registration accepted an out-of-bounds index");
    float strip_positions[]{-1, -1, 3, 1, -1, 3, -1, 1, 3, 1, 1, 3};
    uint32_t strip_indices[]{0, 1, 2, 3};
    const auto strip = cc::geometry::register_resource(
        strip_positions, 4, nullptr, 0, {{4, strip_indices, nullptr, nullptr, true, false}});
    require(strip->indices == std::vector<uint32_t>{0, 1, 2, 2, 1, 3} && strip->ranges[0].two_sided,
            "Registered strip changed legacy winding or two-sided behavior");
    zvid::set_render3d(nullptr);
    std::cout << "Early geometry handoff, skipped CPU loops, immutable revisions, FP and strict "
                 "rejection pass\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
