cc::geometry::Submission
submit_prepared_geometry(const ::platform::ModelDrawContext &context,
                         const cc::geometry::Fog &fog) override {
  VisualFloatScope preserve;
  if (geometry_mode_ == cc::geometry::Mode::legacy ||
      !prepared_.has_binding(context))
    return cc::geometry::Submission::unsupported;
  const auto selected = prepared_.draw(context, true);
  if (!selected.replace)
    return cc::geometry::Submission::unsupported;
  ensure_geometry_scene();
  for (const auto &draw : selected.batches)
    emit_gpu_prepared(draw, context, fog);
  ++early_prepared_models_;
  ++geometry_statistics_.submitted_models;
  geometry_statistics_.skipped_vertices += context.source_vertices;
  geometry_statistics_.skipped_polygons += context.source_polygons;
  return cc::geometry::Submission::submitted;
}

void ensure_geometry_scene() {
  ensure_extent();
  if (scene_.hud && !scene_.draws.empty()) {
    flush();
    ensure_extent();
  }
  scene_.hud = false;
}

static cc::geometry::Fog prepared_fog() {
  const auto &source = zmod::fog();
  cc::geometry::Fog fog;
  fog.enabled = source.on != 0;
  std::copy_n(source.color, 3, fog.color.begin());
  fog.near_range = source.near_range;
  fog.inverse_range = source.inv_range;
  fog.low_altitude = source.alt_low;
  fog.inverse_altitude = source.inv_alt;
  return fog;
}

void emit_gpu_prepared(const PreparedBatchDraw &source,
                       const ::platform::ModelDrawContext &context,
                       const cc::geometry::Fog &fog) {
  if (!source.deformed.empty())
    throw std::runtime_error(
        "GPU prepared geometry does not admit CPU-deformed vertices");
  if (!(context.near_z > 0) ||
      !(context.far_z > std::max(context.near_z, context.clip_near)))
    throw std::runtime_error(
        "GPU prepared geometry requires valid clip planes");
  for (const auto value :
       {context.fx_near, context.fy_near, context.cx, context.cy,
        context.near_z, context.clip_near, context.far_z})
    if (!std::isfinite(value))
      throw std::runtime_error("Nonfinite prepared projection");
  const auto geometry = prepared_geometry(*source.batch);
  const auto texture = prepared_texture(*source.texture);
  const auto normal =
      source.normal && lighting_.normals ? prepared_normal(*source.normal) : -1;
  if (geometry_mode_ == cc::geometry::Mode::cpu_reference) {
    // This deliberately slow oracle evaluates the same modern fog/material
    // policy and hardware clipping inputs, independently of the GPU stream.
    cc::geometry::Resource reference;
    reference.vertices = geometry->vertices;
    reference.indices.reserve(reference.vertices.size());
    for (std::uint32_t index = 0; index < reference.vertices.size(); ++index)
      reference.indices.push_back(index);
    reference.ranges.push_back(
        {0, std::uint32_t(reference.indices.size()), 0, true, 0});
    cc::geometry::Material material;
    material.texture = texture;
    material.color = source.batch->color;
    const auto first = scene_.draws.size();
    emit_reference_geometry(reference, {material}, context, source.camera,
                            source.world, fog);
    for (auto index = first; index < scene_.draws.size(); ++index) {
      auto &draw = scene_.draws[index];
      apply_surface_material(draw, source, lighting_.normals);
      draw.sampler |= 0x10000U;
      draw.push.mode[3] = 0; // Preserve prepared smooth-normal orientation.
      if (normal >= 0)
        draw.normal_texture = int(bind_texture(normal, false));
    }
    prepared_reference_vertices_ += reference.vertices.size();
  } else {
    auto instance = std::make_shared<vk::ModelInstance>();
    instance->geometry = geometry;
    instance->gpu_vertices = instance->sun_eligible = true;
    instance->camera = matrix_rows(source.camera);
    instance->sun_world = matrix_rows(source.world);
    instance->sun_normal = mw34::sun::normal_matrix(source.world);
    instance->color = source.batch->color;
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
    vk::Draw draw{};
    apply_surface_material(draw, source, lighting_.normals);
    draw.model = std::move(instance);
    draw.geometry_debug_normals = geometry_debug_;
    draw.count = unsigned(geometry->vertices.size());
    draw.texture = int(bind_texture(texture, false));
    if (normal >= 0)
      draw.normal_texture = int(bind_texture(normal, false));
    draw.sampler = 3U | (1U << 4) | (1U << 8) | 0x10000U;
    draw.key = {0, 5, 6, 1, 7, 1, 1, 0};
    draw.push.target[0] = float(scene_.width);
    draw.push.target[1] = float(scene_.height);
    std::copy(fog.color.begin(), fog.color.end(), draw.push.fog);
    draw.push.mode[0] = 2U | 4U | (fog.enabled && !geometry_debug_ ? 16U : 0U);
    draw.push.mode[1] = 4U | (2U << 8);
    draw.push.mode[2] = 2U | (2U << 8);
    draw.scissor = {{0, 0}, {scene_.width, scene_.height}};
    scene_.order.push_back({false, unsigned(scene_.draws.size())});
    scene_.draws.push_back(std::move(draw));
    gpu_prepared_vertices_ += geometry->vertices.size();
  }
  ++prepared_batches_;
}
