void emit_reference_geometry(
    const cc::geometry::Resource &resource,
    const std::vector<cc::geometry::Material> &materials,
    const ::platform::ModelDrawContext &context,
    const ::platform::prepared::Matrix &camera,
    const ::platform::prepared::Matrix &world, const cc::geometry::Fog &fog) {
  const auto normal_matrix = mw34::sun::normal_matrix(camera);
  const auto camera_to_world =
      mw34::donor::product(world, mw34::donor::inverse(camera));
  std::vector<cc::geometry::ReferenceVertex> vertices;
  vertices.reserve(resource.vertices.size());
  for (const auto &source : resource.vertices) {
    cc::geometry::ReferenceVertex vertex;
    const float p[]{source.x, source.y, source.z},
        n[]{source.nx, source.ny, source.nz};
    for (unsigned row = 0; row < 3; ++row) {
      vertex.camera[row] = float(camera[row * 4 + 3]);
      for (unsigned axis = 0; axis < 3; ++axis) {
        vertex.camera[row] += float(camera[row * 4 + axis]) * p[axis];
        vertex.normal[row] += normal_matrix[row * 4 + axis] * n[axis];
      }
    }
    vertex.u = source.u;
    vertex.v = source.v;
    if (fog.enabled) {
      const float height =
          float(world[4] * p[0] + world[5] * p[1] + world[6] * p[2] + world[7]);
      const float distance = std::hypot(vertex.camera[0], vertex.camera[2]);
      const float density =
          std::clamp((distance - fog.near_range) * fog.inverse_range, 0.f, 1.f);
      const float altitude = std::clamp(
          1.f - (height - fog.low_altitude) * fog.inverse_altitude, 0.f, 1.f);
      vertex.visibility = 1.f - density * altitude;
    }
    vertices.push_back(vertex);
  }
  auto view = std::make_shared<mw34::sun::Tl>();
  view->camera_world = matrix_rows(camera_to_world);
  view->projection = {context.near_z,
                      context.fx_near * scene_.width / viewport_[2],
                      context.fy_near * scene_.height / viewport_[3],
                      (context.cx - viewport_[0]) * scene_.width / viewport_[2],
                      (context.cy - viewport_[1]) * scene_.height /
                          viewport_[3],
                      context.far_z};
  for (std::size_t range_index = 0; range_index < resource.ranges.size();
       ++range_index) {
    const auto &range = resource.ranges[range_index];
    const auto &material = materials[range_index];
    vk::Draw draw{};
    draw.geometry_debug_normals = geometry_debug_;
    draw.first = unsigned(scene_.vertices.size() / sizeof(TLVertex));
    draw.texture =
        material.texture < 0 ? -1 : int(bind_texture(material.texture, false));
    draw.sampler = 3U | unsigned(material.address_u) << 4 |
                   unsigned(material.address_v) << 8;
    draw.key = {0, 5, 6, 1, 7, 1, std::uint8_t(range.two_sided ? 1 : 3), 0};
    draw.sun_tl = view;
    draw.push.target[0] = float(scene_.width);
    draw.push.target[1] = float(scene_.height);
    std::copy(fog.color.begin(), fog.color.end(), draw.push.fog);
    draw.push.mode[0] = 2U | (material.texture >= 0 ? 4U : 0U) |
                        (fog.enabled && !geometry_debug_ ? 16U : 0U);
    draw.push.mode[1] = material.texture >= 0 ? 4U | (2U << 8) : 2U;
    draw.push.mode[2] = material.texture >= 0 ? 2U | (2U << 8) : 2U;
    draw.push.mode[3] = 0x80000000U;
    draw.scissor = {{0, 0}, {scene_.width, scene_.height}};
    auto normals = std::make_shared<std::vector<std::array<float, 3>>>();
    for (std::uint32_t triangle = range.first;
         triangle < range.first + range.count; triangle += 3) {
      std::vector<cc::geometry::ReferenceVertex> clipped{
          vertices[resource.indices[triangle]],
          vertices[resource.indices[triangle + 1]],
          vertices[resource.indices[triangle + 2]]};
      clipped = cc::geometry::clip_reference(
          clipped, std::max(context.near_z, context.clip_near), true);
      clipped = cc::geometry::clip_reference(clipped, context.far_z, false);
      for (std::size_t corner = 2; corner < clipped.size(); ++corner) {
        for (std::size_t index : {std::size_t{0}, corner - 1, corner}) {
          const auto &vertex = clipped[index];
          const float depth = context.near_z / vertex.camera[2];
          TLVertex tl{
              vertex.camera[0] * view->projection[1] * depth +
                  view->projection[3],
              vertex.camera[1] * view->projection[2] * depth +
                  view->projection[4],
              depth,
              depth,
              material.color,
              std::uint32_t(std::clamp(vertex.visibility * 255.f, 0.f, 255.f))
                  << 24,
              vertex.u,
              vertex.v};
          const auto *bytes = reinterpret_cast<const unsigned char *>(&tl);
          scene_.vertices.insert(scene_.vertices.end(), bytes,
                                 bytes + sizeof tl);
          normals->push_back(vertex.normal);
        }
        ++cpu_clipped_triangles_;
      }
    }
    draw.count = unsigned(normals->size());
    draw.lit_normals = std::move(normals);
    if (draw.count) {
      scene_.order.push_back({false, unsigned(scene_.draws.size())});
      scene_.draws.push_back(std::move(draw));
    }
  }
}
