cc::geometry::Mode geometry_mode() const override { return geometry_mode_; }
cc::geometry::Statistics geometry_statistics() const override {
  return geometry_statistics_;
}
void note_cpu_model_geometry() override {
  ++geometry_statistics_.unsupported_models;
}

cc::geometry::Submission
submit_model_geometry(std::shared_ptr<const cc::geometry::Resource> source,
                      const ::platform::ModelDrawContext &context,
                      const std::vector<cc::geometry::Material> &materials,
                      const cc::geometry::Fog &fog) override {
  VisualFloatScope preserve;
  if (geometry_mode_ == cc::geometry::Mode::legacy)
    return cc::geometry::Submission::unsupported;
  // Prepared replacements must retain their selected visual until their
  // deformation path joins this same early boundary in the next slice.
  if (prepared_.has_binding(context))
    return cc::geometry::Submission::unsupported;
  if (!source || source->ranges.size() != materials.size() ||
      !context.ordinary || context.morphing || context.overlay ||
      context.opacity != 1.f || !context.model_to_world ||
      !context.model_to_camera || !(context.near_z > 0) ||
      !(context.far_z > std::max(context.near_z, context.clip_near)))
    return cc::geometry::Submission::unsupported;
  ensure_extent();
  if (scene_.hud && !scene_.draws.empty()) {
    flush();
    ensure_extent();
  }
  scene_.hud = false;
  auto matrix = [](const float *source) -> ::platform::prepared::Matrix {
    for (unsigned i = 0; i < 12; ++i)
      if (!std::isfinite(source[i]))
        throw std::runtime_error("Nonfinite model transform");
    return {source[0], source[3],  source[6], source[9], source[1], source[4],
            source[7], source[10], source[2], source[5], source[8], source[11],
            0,         0,          0,         1};
  };
  const auto camera = matrix(context.model_to_camera),
             world = matrix(context.model_to_world);
  if (geometry_mode_ == cc::geometry::Mode::cpu_reference) {
    emit_reference_geometry(*source, materials, context, camera, world, fog);
    ++geometry_statistics_.submitted_models;
    return cc::geometry::Submission::submitted;
  }
  for (auto iterator = native_geometry_.begin();
       iterator != native_geometry_.end();) {
    if (iterator->second.source.expired())
      iterator = native_geometry_.erase(iterator);
    else
      ++iterator;
  }
  auto &cached = native_geometry_[source->identity];
  if (!cached.geometry) {
    cached.source = source;
    auto geometry = std::make_shared<vk::ModelGeometry>();
    geometry->key = 0x4700000000000000ULL | source->identity;
    geometry->vertices = source->vertices;
    geometry->indices = source->indices;
    geometry->source = source;
    geometry->hash = 0xcbf29ce484222325ULL;
    const auto hash = [&](const void *data, std::size_t bytes) {
      const auto *input = static_cast<const unsigned char *>(data);
      for (std::size_t i = 0; i < bytes; ++i) {
        geometry->hash ^= input[i];
        geometry->hash *= 0x100000001b3ULL;
      }
    };
    hash(geometry->vertices.data(),
         geometry->vertices.size() * sizeof(vk::ModelVertex));
    hash(geometry->indices.data(),
         geometry->indices.size() * sizeof(std::uint32_t));
    cached.geometry = std::move(geometry);
  }
  auto instance = std::make_shared<vk::ModelInstance>();
  instance->geometry = cached.geometry;
  instance->source = source;
  instance->gpu_vertices = true;
  instance->camera = matrix_rows(camera);
  instance->sun_world = matrix_rows(world);
  instance->sun_normal = mw34::sun::normal_matrix(world);
  instance->sun_eligible = true;
  instance->geometry_near = context.clip_near;
  instance->geometry_fog0 = {fog.near_range, fog.inverse_range,
                             fog.low_altitude, fog.inverse_altitude};
  instance->geometry_fog1 = {0, 0, fog.enabled ? 1.f : 0.f, 0};
  instance->projection = {
      context.near_z,
      context.fx_near * scene_.width / viewport_[2],
      context.fy_near * scene_.height / viewport_[3],
      (context.cx - viewport_[0]) * scene_.width / viewport_[2] - .5f,
      (context.cy - viewport_[1]) * scene_.height / viewport_[3] - .5f,
      context.far_z};
  // Validate every binding before modifying the ordered scene packet.
  for (const auto &material : materials)
    if (material.texture >= 0)
      (void)textures_.at(std::size_t(material.texture));
  for (std::size_t range_index = 0; range_index < source->ranges.size();
       ++range_index) {
    const auto &range = source->ranges[range_index];
    const auto &material = materials[range_index];
    vk::Draw draw{};
    draw.geometry_debug_normals = geometry_debug_;
    auto material_instance = std::make_shared<vk::ModelInstance>(*instance);
    material_instance->color = material.color;
    draw.model = std::move(material_instance);
    draw.first = range.first;
    draw.count = range.count;
    draw.texture =
        material.texture < 0 ? -1 : int(bind_texture(material.texture, false));
    draw.sampler = 3U | unsigned(material.address_u) << 4 |
                   unsigned(material.address_v) << 8;
    draw.key = {0, 5, 6, 1, 7, 1, std::uint8_t(range.two_sided ? 1 : 3), 0};
    draw.push.target[0] = float(scene_.width);
    draw.push.target[1] = float(scene_.height);
    std::copy(fog.color.begin(), fog.color.end(), draw.push.fog);
    draw.push.mode[0] = 2U | (material.texture >= 0 ? 4U : 0U) |
                        (fog.enabled && !geometry_debug_ ? 16U : 0U);
    draw.push.mode[1] = material.texture >= 0 ? 4U | (2U << 8) : 2U;
    draw.push.mode[2] = material.texture >= 0 ? 2U | (2U << 8) : 2U;
    draw.push.mode[3] = 0x80000000U; // Modern two-sided normal orientation.
    draw.scissor = {{0, 0}, {scene_.width, scene_.height}};
    scene_.order.push_back({false, unsigned(scene_.draws.size())});
    scene_.draws.push_back(std::move(draw));
  }
  ++geometry_statistics_.submitted_models;
  geometry_statistics_.skipped_vertices += source->source_vertices;
  geometry_statistics_.skipped_polygons += source->source_polygons;
  return cc::geometry::Submission::submitted;
}
