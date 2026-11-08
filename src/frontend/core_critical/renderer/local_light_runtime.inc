// Producer-owned local light state; packets receive only the fixed Uniform.
mw34::effects::Catalog local_catalog_;
mw34::effects::ObservedLights local_observed_;
std::vector<mw34::effects::Candidate> local_native_;
std::array<float, 6> local_projection_{};
std::array<float, 12> local_camera_{};
bool local_active_{};
double local_time_{};
std::uint64_t local_observations_{}, local_native_peak_{}, local_packet_peak_{};

void local_lights_load() {
  if (!bridge_config_.effect_lights)
    return;
  VisualFloatScope guard;
  if (!lighting_.enabled || bridge_config_.effect_light_catalog.empty())
    throw std::runtime_error("Local lights require lighting and a catalog");
  std::ifstream stream(
      std::filesystem::u8path(bridge_config_.effect_light_catalog),
      std::ios::binary);
  if (!stream)
    throw std::runtime_error("effect light catalog missing");
  local_catalog_ = mw34::effects::read_catalog(stream);
}

void local_lights_clear() {
  local_observed_.clear();
  local_native_.clear();
  local_active_ = false;
  local_camera_ = {};
  local_projection_ = {};
  local_time_ = 0.0;
}

void local_lights_begin() {
  local_active_ = false;
  local_native_.clear();
  if (!bridge_config_.effect_lights || current_mission_id_.empty())
    return;
  VisualFloatScope guard;
  const auto p = zmth::projection();
  local_projection_ = {p.near_z, p.fx_near, p.fy_near, p.cx, p.cy, p.far_z};
  if (!std::isfinite(local_projection_[0]) ||
      !std::isfinite(local_projection_[1]) ||
      !std::isfinite(local_projection_[2]) ||
      !std::isfinite(local_projection_[3]) ||
      !std::isfinite(local_projection_[4]) ||
      !std::isfinite(local_projection_[5]))
    throw std::runtime_error("Local lights: non-finite projection");
  if (local_projection_[0] <= 0.0f || local_projection_[1] <= 0.0f ||
      local_projection_[2] <= 0.0f || local_projection_[5] <= 0.0f)
    throw std::runtime_error("Local lights: invalid projection");
  const float *m = zmth::view().camera.m;
  local_camera_ = {m[0], m[3],  m[6], m[9], m[1], m[4],
                   m[7], m[10], m[2], m[5], m[8], m[11]};
  for (float v : local_camera_)
    if (!std::isfinite(v))
      throw std::runtime_error("Local lights: invalid camera");
  local_time_ = ztimer::state().total_scaled;
  if (!std::isfinite(local_time_) || local_time_ < 0.0)
    throw std::runtime_error("Local lights: invalid time");
  if (bridge_config_.fixture_lights) {
    local_native_ = mw34::effects::collect_native_lights(zmod::light_list());
    local_native_peak_ = std::max(local_native_peak_,
                                  static_cast<uint64_t>(local_native_.size()));
  }
  local_active_ = true;
}

void local_lights_observe(const TLVertex *vertices, int count,
                          const DrawState &state) {
  if (!bridge_config_.effect_lights || !local_active_ || state.sharp ||
      state.texture < 0 ||
      state.texture >= static_cast<int>(textures_.size()) || count <= 0) {
    return;
  }
  auto it = local_catalog_.find(textures_[state.texture].surface_key);
  if (it == local_catalog_.end()) {
    return;
  }
  if (!vertices || count > 1024) {
    throw std::runtime_error("local_lights_observe: invalid vertex buffer");
  }
  VisualFloatScope guard;
  std::vector<mw34::effects::Point> points;
  points.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    points.push_back(mw34::effects::reconstruct(
        vertices[i].sx, vertices[i].sy, vertices[i].sz, local_projection_,
        local_camera_));
  }
  local_observed_.observe(it->second, points, local_time_);
  ++local_observations_;
}

void local_lights_attach(vk::Scene &target) {
  target.effect_lights = {};
  if (!bridge_config_.effect_lights || !local_active_ || target.hud ||
      target.frontend_canvas) {
    return;
  }
  VisualFloatScope guard;
  std::vector<mw34::effects::Candidate> sources = local_native_;
  auto candidates = local_observed_.candidates(local_time_);
  sources.insert(sources.end(), candidates.begin(), candidates.end());
  mw34::effects::Point eye = {local_camera_[3], local_camera_[7],
                              local_camera_[11]};
  target.effect_lights = mw34::effects::rank_frame_lights(sources, eye, 64, 16);
  local_packet_peak_ =
      std::max(local_packet_peak_,
               static_cast<std::uint64_t>(target.effect_lights.params[0]));
}

void local_lights_report() {
  if (!bridge_config_.effect_lights) {
    return;
  }
  fprintf(
      stderr,
      "[LOCAL_LIGHTS] observations=%llu native_peak=%llu packet_peak=%llu\n",
      static_cast<unsigned long long>(local_observations_),
      static_cast<unsigned long long>(local_native_peak_),
      static_cast<unsigned long long>(local_packet_peak_));
}
