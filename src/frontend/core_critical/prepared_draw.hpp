#pragma once

#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "prepared_models.hpp"
#include "renderer/donor_retarget.hpp"
#include "renderer/vk_scene.hpp"

namespace cc_mw3_re {

inline std::array<float, 12>
matrix_rows(const ::platform::prepared::Matrix &matrix) {
  std::array<float, 12> result{};
  for (unsigned i = 0; i < result.size(); ++i) {
    result[i] = float(matrix[i]);
    if (!std::isfinite(result[i]))
      throw std::runtime_error("Prepared transform exceeds float range");
  }
  return result;
}

inline void apply_surface_material(mw34::vkscene::Draw &draw,
                                   const PreparedBatchDraw &source,
                                   bool enabled) {
  draw.surface_parameters = {};
  draw.surface_eye = {};
  if (!enabled || !source.surface_material)
    return;
  const auto &material = *source.surface_material;
  const auto eye = matrix_rows(
      mw34::donor::product(source.world, mw34::donor::inverse(source.camera)));
  draw.surface_parameters = {material.roughness, material.reflectivity,
                             material.metallic, 1.f};
  draw.surface_eye = {eye[3], eye[7], eye[11], 1.f};
}

struct PreparedVertex {
  std::array<float, 3> camera{}, normal{}, color{};
  float visibility{}, u{}, v{};
};

inline std::vector<PreparedVertex> clip_depth(std::vector<PreparedVertex> input,
                                              float plane, bool near_plane) {
  std::vector<PreparedVertex> result;
  for (size_t i = 0; i < input.size(); ++i) {
    const auto &a = input[i];
    const auto &b = input[(i + 1) % input.size()];
    const bool inside_a =
        near_plane ? a.camera[2] >= plane : a.camera[2] <= plane;
    const bool inside_b =
        near_plane ? b.camera[2] >= plane : b.camera[2] <= plane;
    if (inside_a)
      result.push_back(a);
    if (inside_a != inside_b) {
      const float t = (plane - a.camera[2]) / (b.camera[2] - a.camera[2]);
      PreparedVertex vertex;
      for (unsigned axis = 0; axis < 3; ++axis) {
        vertex.camera[axis] = std::lerp(a.camera[axis], b.camera[axis], t);
        vertex.normal[axis] = std::lerp(a.normal[axis], b.normal[axis], t);
        vertex.color[axis] = std::lerp(a.color[axis], b.color[axis], t);
      }
      vertex.camera[2] = plane;
      vertex.visibility = std::lerp(a.visibility, b.visibility, t);
      vertex.u = std::lerp(a.u, b.u, t);
      vertex.v = std::lerp(a.v, b.v, t);
      result.push_back(vertex);
    }
  }
  return result;
}

struct PreparedSubmission {
  mw34::vkscene::Draw draw{};
  std::vector<unsigned char> vertices;
  uint64_t clipped_triangles{};
};

struct PreparedDrawCpuStatistics {
  std::uint64_t vertices{}, stage_ticks{}, assembly_ticks{};
};

// Keeps clipping, native projection and fog identical for both material modes.
// Lit CPU vertices carry smooth normals in a transient stream, not immutable
// VBOs.
inline PreparedSubmission
prepare_draw(const PreparedBatchDraw &source,
             const ::platform::ModelDrawContext &context,
             const std::array<int, 4> &viewport, unsigned width,
             unsigned height,
             std::shared_ptr<const mw34::vkscene::ModelGeometry> geometry,
             bool lighting, PreparedDrawCpuStatistics *statistics = nullptr) {
  namespace vk = mw34::vkscene;
  if (!(context.near_z > 0) || !(context.far_z > context.near_z))
    throw std::runtime_error(
        "Prepared model requires valid native clip planes");
  const auto camera = matrix_rows(source.camera);
  const auto normal_matrix =
      lighting ? mw34::sun::normal_matrix(source.camera) : camera;
  const auto &vertices =
      source.deformed.empty() ? source.batch->vertices : source.deformed;
  const float near_clip = std::max(context.near_z, context.clip_near);
  bool gpu = source.deformed.empty();
  std::vector<PreparedVertex> staged;
  staged.reserve(vertices.size());
  if (statistics)
    statistics->vertices += vertices.size();
  {
    sdlfe::CpuInterval interval(statistics ? &statistics->stage_ticks
                                           : nullptr);
    for (const auto &vertex : vertices) {
      PreparedVertex output;
      for (unsigned row = 0; row < 3; ++row) {
        output.camera[row] =
            ((camera[row * 4] * vertex[0] + camera[row * 4 + 1] * vertex[1]) +
             camera[row * 4 + 2] * vertex[2]) +
            camera[row * 4 + 3];
        output.normal[row] = (normal_matrix[row * 4] * vertex[3] +
                              normal_matrix[row * 4 + 1] * vertex[4]) +
                             normal_matrix[row * 4 + 2] * vertex[5];
        if (!std::isfinite(output.camera[row]) ||
            !std::isfinite(output.normal[row]))
          throw std::runtime_error("Prepared vertex transform is nonfinite");
      }
      const float length =
          std::hypot(output.normal[0], output.normal[1], output.normal[2]);
      if (!(length > 0) || !std::isfinite(length))
        throw std::runtime_error(
            "Prepared vertex has a degenerate transformed normal");
      const float light =
          .35f + .65f * std::max(0.f, (.3f * output.normal[0] +
                                       .6f * output.normal[1] -
                                       .74f * output.normal[2]) /
                                          length);
      for (unsigned channel = 0; channel < 3; ++channel)
        output.color[channel] = std::floor(
            float((source.batch->color >> (16 - channel * 8)) & 255) * light);
      output.visibility =
          zmod::fog().on ? 1.f - zmod::fog_at(output.camera.data()) : 1.f;
      output.u = vertex[6];
      output.v = vertex[7];
      gpu = gpu && output.camera[2] >= near_clip &&
            output.camera[2] <= context.far_z;
      staged.push_back(output);
    }
  }
  sdlfe::CpuInterval assembly(statistics ? &statistics->assembly_ticks
                                         : nullptr);
  PreparedSubmission result;
  auto &draw = result.draw;
  draw.texture = -1;
  draw.sampler = 3U | (1U << 4) | (1U << 8) | 0x10000U;
  draw.key = {0, 5, 6, 1, 7, 1, 1, 0};
  draw.push.target[0] = float(width);
  draw.push.target[1] = float(height);
  std::copy(std::begin(zmod::fog().color), std::end(zmod::fog().color),
            draw.push.fog);
  draw.push.mode[0] = 2U | 4U | (zmod::fog().on ? 16U : 0U);
  draw.push.mode[1] = 4U | (2U << 8);
  draw.push.mode[2] = 2;
  draw.scissor = {{0, 0}, {width, height}};
  auto normals = std::make_shared<std::vector<std::array<float, 3>>>();
  const auto append = [&](const PreparedVertex &vertex, bool project) {
    uint32_t color = source.batch->color & 0xff000000U;
    for (unsigned channel = 0; channel < 3; ++channel)
      color |= uint32_t(std::clamp(vertex.color[channel], 0.f, 255.f))
               << (16 - channel * 8);
    const auto fog =
        uint32_t(std::clamp(vertex.visibility * 255.f, 0.f, 255.f));
    ::platform::TLVertex tl{0, 0, 0, 1, color, fog << 24, vertex.u, vertex.v};
    if (project) {
      tl.rhw = tl.sz = context.near_z / vertex.camera[2];
      tl.sx = ((context.fx_near * vertex.camera[0]) * tl.rhw + context.cx -
               viewport[0]) *
              width / float(viewport[2]);
      tl.sy = ((context.fy_near * vertex.camera[1]) * tl.rhw + context.cy -
               viewport[1]) *
              height / float(viewport[3]);
      if (lighting)
        normals->push_back(vertex.normal);
    }
    const auto *bytes = reinterpret_cast<const unsigned char *>(&tl);
    result.vertices.insert(result.vertices.end(), bytes, bytes + sizeof tl);
  };
  const std::array<float, 6> projection{
      context.near_z,
      context.fx_near * width / viewport[2],
      context.fy_near * height / viewport[3],
      (context.cx - viewport[0]) * width / viewport[2] - .5f,
      (context.cy - viewport[1]) * height / viewport[3] - .5f,
      context.far_z};
  if (gpu) {
    result.vertices.reserve(staged.size() * sizeof(::platform::TLVertex));
    for (const auto &vertex : staged)
      append(vertex, false);
    auto instance = std::make_shared<vk::ModelInstance>();
    instance->geometry = std::move(geometry);
    instance->camera = camera;
    instance->projection = projection;
    instance->sun_world = matrix_rows(source.world);
    instance->sun_normal = mw34::sun::normal_matrix(source.world);
    instance->world = instance->sun_eligible = true;
    draw.model = std::move(instance);
  } else {
    for (size_t i = 0; i < staged.size(); i += 3) {
      auto clipped = clip_depth({staged[i], staged[i + 1], staged[i + 2]},
                                near_clip, true);
      clipped = clip_depth(std::move(clipped), context.far_z, false);
      for (size_t j = 1; j + 1 < clipped.size(); ++j) {
        append(clipped[0], true);
        append(clipped[j], true);
        append(clipped[j + 1], true);
        ++result.clipped_triangles;
      }
    }
    if (lighting) {
      auto view = std::make_shared<mw34::sun::Tl>();
      view->camera_world = matrix_rows(mw34::donor::product(
          source.world, mw34::donor::inverse(source.camera)));
      view->projection = projection;
      // TL coordinates already include D3D pixel centres, unlike the GPU
      // model projection. Its camera reconstruction needs the exact centre.
      view->projection[3] += .5f;
      view->projection[4] += .5f;
      draw.sun_tl = std::move(view);
      draw.lit_normals = std::move(normals);
    }
  }
  draw.count = unsigned(result.vertices.size() / sizeof(::platform::TLVertex));
  return result;
}

} // namespace cc_mw3_re
