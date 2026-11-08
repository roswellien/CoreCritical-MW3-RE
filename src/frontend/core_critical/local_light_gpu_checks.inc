void local_light_test_env(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}
// Qwen test structure integrated against the actual native/renderer ABI.
void check_local_light_gpu(SDL_Window *window, sdlfe::RendererBackend backend) {
  const auto old_list = zmod::light_list();
  const auto old_projection = zmth::projection();
  const auto old_view = zmth::view();
  struct Env {
    const char *name;
    std::optional<std::string> prior;
    explicit Env(const char *n) : name(n) {
      if (const char *v = std::getenv(n))
        prior = v;
    }
    ~Env() {
      if (prior)
        local_light_test_env(name, prior->c_str());
      else
        local_light_test_env(name, nullptr);
    }
  } catalog_env("MW34_EFFECT_LIGHT_CATALOG"), enabled_env("MW34_EFFECT_LIGHTS"),
      fixtures_env("MW34_FIXTURE_LIGHTS");
  const auto path = std::filesystem::temp_directory_path() /
                    ("mw34-wp04a-" + std::to_string(SDL_GetTicksNS()) + ".bin");
  struct Restore {
    std::function<void()> f;
    ~Restore() { f(); }
  } restore{[&] {
    mw34::materials::clear_active_surface_key();
    zmod::light_list() = old_list;
    zmth::projection() = old_projection;
    zmth::view() = old_view;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }};
  {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t n) {
      for (unsigned i = 0; i < 4; ++i)
        f.put(char(n >> (i * 8)));
    };
    auto scalar = [&](float n) {
      uint32_t bits;
      std::memcpy(&bits, &n, 4);
      u32(bits);
    };
    f.write("MWFX1\0\0\0", 8);
    u32(1);
    u32(1);
    u32(1);
    u32(1);
    u32(2);
    u32(123);
    u32(0);
    scalar(10);
    scalar(1);
    scalar(1);
    scalar(1);
    scalar(1);
    scalar(1);
    f.flush();
    require(bool(f), "Could not write local-light fixture catalog");
  }
  local_light_test_env("MW34_EFFECT_LIGHT_CATALOG", path.string().c_str());
  zmth::projection() = {};
  auto &projection = zmth::projection();
  projection.near_z = 1;
  projection.fx_near = projection.fy_near = 16;
  projection.cx = projection.cy = 16;
  projection.far_z = 100;
  const float camera[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  std::memcpy(zmth::view().camera.m, camera, sizeof camera);
  zcls::Light data{};
  data.put<uint32_t>(0xb0, 0x202);
  data.put<float>(0xb8, 10);
  data.put<float>(0x9c, 2);
  data.put<float>(0xa4, 1);
  data.put<float>(0xa8, 1);
  data.put<float>(0xac, 1);
  data.put<float>(0x14, 1);
  const auto original = data;
  zcls::Node node{};
  node.put<int32_t>(0x34, 9);
  node.data = &data;
  zmod::light_list() = {};
  zmod::light_list().count = 1;
  zmod::light_list().list[0].data = &data;
  zmod::light_list().list[0].node = &node;
  auto render = [&](bool enabled, bool mutate, bool weapon_source = false,
                    bool sharp_source = false,
                    bool overlay_before_emitter = false,
                    bool ambient_only = false) {
    data = original;
    if (ambient_only) {
      data.put<float>(0x9c, 0);
      data.put<float>(0xa0, 2);
    }
    const auto expected_source = data;
    local_light_test_env("MW34_EFFECT_LIGHTS", enabled ? "1" : "0");
    local_light_test_env("MW34_FIXTURE_LIGHTS",
                         enabled && !weapon_source ? "1" : "0");
    sdlfe::CoreCriticalAssets assets{};
    assets.lighting = "flat";
    auto renderer = make_check_renderer(window, backend, {}, assets);
    renderer->mission_changed({5, 1});
    renderer->set_viewport(0, 0, 32, 32);
    const uint8_t grey[4] = {128, 128, 128, 255};
    // Synthetic authenticated receiver identity; distinct from catalog emitter.
    mw34::materials::set_active_surface_key({1, 1, 1, 456});
    platform::DrawState state{};
    state.texture = renderer->create_texture(1, 1, grey, 1);
    mw34::materials::clear_active_surface_key();
    renderer->clear(true, 0, 0, 0);
    renderer->begin_scene();
    if (mutate) {
      data.put<float>(0x9c, 0);
      data.put<float>(0x14, 1000);
    }
    quad(*renderer, 4, 28, .5f, 0xffffffff, state);
    if (overlay_before_emitter) {
      platform::DrawState overlay = state;
      overlay.depth_always = true;
      overlay.depth_write = false;
      quad(*renderer, 28, 30, .5f, 0xffffffff, overlay);
    }
    if (weapon_source) {
      const uint8_t white[4] = {255, 255, 255, 255};
      mw34::materials::set_active_surface_key({1, 1, 1, 123});
      const auto emitter = renderer->create_texture(1, 1, white, 1);
      mw34::materials::clear_active_surface_key();
      platform::DrawState source{};
      source.texture = emitter;
      source.depth_write = false;
      source.sharp = sharp_source;
      // The sprite never covers the sampled receiver pixel.
      quad(*renderer, 24, 28, .8f, 0xffffffff, source);
    }
    renderer->end_scene();
    std::vector<uint32_t> pixels;
    int w = 0, h = 0;
    require(renderer->read_back(&pixels, &w, &h) && w == 32 && h == 32,
            "Local-light receiver readback failed");
    if (!mutate)
      require(!std::memcmp(data.raw, expected_source.raw, sizeof data.raw),
              "Local-light renderer mutated native source");
    return pixels.at(16 * 32 + 16);
  };
  const auto off = render(false, false), on = render(true, false),
             copied = render(true, true);
  require(on != off, "Native fixture did not change the GPU receiver");
  require(
      copied == on,
      "Native light source changed after BeginScene altered the owned packet");
  const auto weapon = render(true, false, true);
  require(weapon != off,
          "Authenticated emitter did not illuminate the receiver");
  require(render(true, false, true, false, true) == weapon,
          "Overlay submission omitted a later emitter from earlier receivers");
  require(render(true, false, false, false, false, true) == on,
          "Ambient-only native source was discarded or changed intensity");
  require(render(true, false, true, true) == off,
          "HUD sprite incorrectly admitted as an emitter");
  // Budget rejection must retain the scene. Retrying the same unchanged
  // submission must reject again, rather than silently succeeding after loss.
  {
    sdlfe::CoreCriticalAssets assets{};
    assets.lighting = "flat";
    auto renderer = make_check_renderer(window, backend, {}, assets);
    renderer->mission_changed({5, 1});
    renderer->set_viewport(0, 0, 32, 32);
    const std::vector<uint8_t> pixels(std::size_t(4096) * 4096 * 4, 128);
    platform::DrawState first{}, second{};
    first.texture = renderer->create_texture(4096, 4096, pixels.data(), 1);
    second.texture = renderer->create_texture(4096, 4096, pixels.data(), 1);
    renderer->clear(true, 0, 0, 0);
    renderer->begin_scene();
    quad(*renderer, 4, 28, .5f, 0xffffffff, first);
    quad(*renderer, 4, 28, .5f, 0xffffffff, second);
    auto rejects_budget = [&] {
      try {
        renderer->end_scene();
      } catch (const std::runtime_error &e) {
        return std::string(e.what()).find("staging exceeds budget") !=
               std::string::npos;
      }
      return false;
    };
    require(rejects_budget(), "Oversized local-light view was not rejected");
    require(rejects_budget(),
            "Rejected local-light view lost its retained payloads");
  }
}
