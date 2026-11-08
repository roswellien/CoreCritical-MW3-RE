// Double-precision analytical oracle, independent of the GPU producer/readback.
bool tiled_expected(const mw34::tiled::View &v, const mw34::effects::Light &l, unsigned x,
                    unsigned y) {
  std::array<double, 3> world{}, center{}, translation{};
  double radius = l.start[3];
  for (unsigned i = 0; i < 3; ++i)
    world[i] = l.start[i];
  if (l.end[3] == 1) {
    double length2 = 0;
    for (unsigned i = 0; i < 3; ++i) {
      const double delta = double(l.end[i]) - l.start[i];
      length2 += delta * delta;
      world[i] = (double(l.end[i]) + l.start[i]) / 2;
    }
    radius += std::sqrt(length2) / 2;
  }
  auto length = [](const auto &a) {
    double sum = 0;
    for (double n : a)
      sum += n * n;
    return std::sqrt(sum);
  };
  double gramBound = 0;
  for (unsigned i = 0; i < 3; ++i) {
    translation[i] = v.camera[i * 4 + 3];
    center[i] = translation[i];
    for (unsigned j = 0; j < 3; ++j)
      center[i] += v.camera[i * 4 + j] * world[j];
    double rowSum = 0;
    for (unsigned j = 0; j < 3; ++j) {
      double dot = 0;
      for (unsigned k = 0; k < 3; ++k)
        dot += double(v.camera[i * 4 + k]) * v.camera[j * 4 + k];
      rowSum += std::abs(dot);
    }
    gramBound = std::max(gramBound, rowSum);
  }
  radius *= std::sqrt(gramBound);
  radius += 1e-5 * (1 + length(world) + length(translation) + length(center) + radius);
  if (center[2] + radius < v.depth[0] || center[2] - radius > v.depth[1])
    return false;
  const double left = x * 16, right = std::min((x + 1) * 16, v.extent[0]);
  const double top = y * 16, bottom = std::min((y + 1) * 16, v.extent[1]);
  const auto &p = v.projection;
  const std::array<std::array<double, 3>, 4> planes{{{p[0], 0, p[2] - left},
                                                     {-p[0], 0, right - p[2]},
                                                     {0, p[1], p[3] - top},
                                                     {0, -p[1], bottom - p[3]}}};
  for (const auto &n : planes) {
    double dot = 0;
    for (unsigned i = 0; i < 3; ++i)
      dot += n[i] * center[i];
    if (dot < -radius * length(n))
      return false;
  }
  return true;
}
mw34::vkscene::Scene make_tiled_view_scene(bool enabled, bool second_near = false) {
  using namespace mw34;
  vkscene::Scene scene{};
  scene.width = 64;
  scene.height = 32;
  scene.has_depth = true;
  scene.color.resize(64 * 32);
  scene.depth.resize(64 * 32);
  scene.textures.push_back({1, 1, 1, 1, vkscene::TexFormat::rgb565, {0x7bef}, {}});
  std::vector<platform::TLVertex> vertices;
  for (unsigned view = 0; view < 2; ++view) {
    const float left = view ? 36.f : 4.f, right = left + 24;
    for (auto p : std::array<std::array<float, 2>, 6>{
             {{left, 4}, {right, 4}, {right, 28}, {left, 4}, {right, 28}, {left, 28}}})
      vertices.push_back({p[0], p[1], .5f, .5f, 0xffffffff, 0xff000000, 0, 0});
    vkscene::Draw draw{};
    draw.first = view * 6;
    draw.count = 6;
    draw.texture = 0;
    draw.key = {0, 5, 6, 1, 7, 1, 1, 0};
    draw.scissor = {{0, 0}, {64, 32}};
    draw.push.target[0] = 64;
    draw.push.target[1] = 32;
    draw.push.target[2] = .5f;
    draw.push.mode[0] = 6;
    draw.push.mode[1] = draw.push.mode[2] = 2 | (2 << 8);
    auto camera = std::make_shared<sun::Tl>();
    camera->camera_world = {1, 0, 0, view && !second_near ? 100.f : 0.f, 0, 1, 0, 0, 0, 0, 1, 0};
    camera->projection = {1, 16, 16, view ? 48.f : 16.f, 16, 100};
    draw.sun_tl = camera;
    draw.lit_normals = std::make_shared<const std::vector<std::array<float, 3>>>(
        6, std::array<float, 3>{0, 0, -1});
    scene.draws.push_back(draw);
    scene.order.push_back({false, view});
  }
  scene.vertices.resize(vertices.size() * sizeof(vertices[0]));
  std::memcpy(scene.vertices.data(), vertices.data(), scene.vertices.size());
  if (enabled) {
    scene.effect_lights.params[0] = 1;
    scene.effect_lights.lights[0] = {{0, 0, 1, 2}, {0, 0, 1, 0}, {1, 1, 1, 1}};
  }
  return scene;
}
void check_tiled_view_render() {
  using namespace mw34;
  sun::Settings settings{};
  settings.enabled = true;
  settings.strength = 0;
  settings.ambient = .1f;
  cc_mw3_re::RendererBridgeConfig config{};
  config.effect_lights = true;
  config.effect_light_catalog = "diagnostic-owned-scene";
  vkscene::Renderer renderer(vkscene::Renderer::ColorTarget::rgba8, {},
                             vkscene::Renderer::Quality{1, 1, 1, false}, true, settings, false,
                             config);
  auto off = make_tiled_view_scene(false);
  renderer.render(off);
  auto on = make_tiled_view_scene(true);
  renderer.render(on);
  constexpr unsigned first = 16 * 64 + 16, second = 16 * 64 + 48;
  require(on.color[first] != off.color[first], "Tiled primary receiver did not illuminate");
  require(on.color[second] == off.color[second],
          "Tiled light leaked into translated secondary camera");
  const auto expected = on.color;
  std::swap(on.draws[0], on.draws[1]);
  renderer.render(on);
  require(on.color == expected, "Tiled fragment view offset changed with draw order");
  auto near = make_tiled_view_scene(true, true);
  renderer.render(near);
  require(near.color[second] != off.color[second],
          "Secondary view fixture did not exercise illumination");
  renderer.render(near, true, false); // GPU still owns these view buffers on return.
  auto reused = make_tiled_view_scene(true);
  renderer.render(reused);
  require(reused.color == expected,
          "Tiled asynchronous view buffers were reused before fence or retained stale lights");
}
void check_tiled_gpu() {
  using namespace mw34;
  {
    const uint8_t rgba[3][4]{{8, 4, 8, 255}, {255, 0, 255, 255}, {0x11, 0x22, 0x33, 0x44}};
    const uint8_t packed[3][2]{{0x21, 0x08}, {0x1f, 0xfc}, {0x23, 0x41}};
    for (unsigned i = 0; i < 3; ++i) {
      const auto key = materials::canonical_upload_key(1, 1, rgba[i], i + 1);
      require(key.hash == materials::normal_hash(packed[i], 2) && key.format == i + 1,
              "Native upload key packed format/hash");
    }
    auto changed = std::array<uint8_t, 4>{0x11, 0x22, 0x33, 0x54};
    require(materials::canonical_upload_key(1, 1, changed.data(), 3).hash !=
                materials::canonical_upload_key(1, 1, rgba[2], 3).hash,
            "Native upload key ignored alpha");
    materials::SurfaceKey original{};
    const bool had = materials::get_active_surface_key(&original);
    const materials::SurfaceKey prior{2, 2, 1, 123}, inner{1, 1, 3, 456};
    {
      materials::ScopedUploadKey outer(prior);
      try {
        materials::ScopedUploadKey nested(inner);
        throw std::runtime_error("scope control");
      } catch (const std::runtime_error &) {
      }
      materials::SurfaceKey restored{};
      require(materials::get_active_surface_key(&restored) && restored == prior,
              "Native upload key exception restoration");
    }
    materials::SurfaceKey restored{};
    require(materials::get_active_surface_key(&restored) == had && (!had || restored == original),
            "Native upload key outer restoration");
  }
  {
    std::vector<uint8_t> large_rgba(8192u * 1024u * 4u, 255);
    const auto large_key = materials::canonical_upload_key(8192, 1024, large_rgba.data(), 1);
    require(large_key.width == 8192 && large_key.height == 1024,
            "Native upload key restricted display-cap-sized texture");

    bool overflow_rejected = false;
    try {
      (void)materials::canonical_upload_key(UINT32_MAX, UINT32_MAX, large_rgba.data(), 1);
    } catch (const std::overflow_error &) {
      overflow_rejected = true;
    }
    require(overflow_rejected, "canonical_upload_key rejects overflow before reading buffer");
  }
  const std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  auto base = tiled::make_view(identity, {32, -32, 32.5f, 24.5f}, 1, 100, 65, 49);
  auto translated = identity;
  translated[3] = -100;
  auto reflected = identity;
  reflected[0] = -1;
  auto skewed = identity;
  skewed[0] = 1.0008f;
  std::vector<tiled::View> views{base,
                                 tiled::make_view(translated, base.projection, 1, 100, 65, 49),
                                 tiled::make_view(reflected, base.projection, 1, 100, 65, 49),
                                 tiled::make_view(skewed, base.projection, 1, 100, 65, 49)};
  effects::Uniform lights{};
  lights.params[0] = 6;
  lights.lights[0] = {{-4, 0, 8, .8f}, {-4, 0, 8, 0}, {1, 1, 1, 1}};
  lights.lights[1] = {{4, 0, 8, 1}, {0, 0, -1, 2}, {1, 1, 1, 1}};
  lights.lights[2] = {{-3, -2, 5, .2f}, {3, 2, 5, 1}, {1, 1, 1, 1}};
  lights.lights[3] = {{0, 0, .9f, .4f}, {0, 0, .9f, 0}, {1, 1, 1, 1}};
  lights.lights[4] = {{0, 0, -5, .1f}, {0, 0, -5, 0}, {1, 1, 1, 1}};
  lights.lights[5] = {{0, 0, 101, .5f}, {0, 0, 101, 0}, {1, 1, 1, 1}};
  vkscene::Renderer renderer;
  {
    auto stretched = identity;
    stretched[0] = 1.0008f;
    auto v = tiled::make_view(stretched, {32, -32, 32, 24}, 1, 100, 65, 49);
    effects::Uniform fx{};
    fx.params[0] = 1;
    fx.lights[0] = {{1, 0, 10, 1}, {1, 0, 10, 0}, {1, 1, 1, 1}};
    auto data = renderer.probe_tiled_lists({v}, fx);
    std::uint32_t offset = (1 * v.extent[2] + 1) * 65;
    require(data[0][offset] == 1 && data[0][offset + 1] == 0,
            "Tiled camera norm bound lost tangent light");
  }
  auto verify = [&](const auto &cameras, const effects::Uniform &fx) {
    const auto data = renderer.probe_tiled_lists(cameras, fx);
    require(data.size() == cameras.size(), "Tiled producer lost a view");
    unsigned populated = 0, empty = 0;
    for (size_t view = 0; view < cameras.size(); ++view) {
      const auto &v = cameras[view];
      require(data[view].size() * 4 == tiled::bytes(v), "Tiled producer readback size");
      for (unsigned y = 0; y < v.extent[3]; ++y)
        for (unsigned x = 0; x < v.extent[2]; ++x) {
          const auto offset = (y * v.extent[2] + x) * 65;
          std::vector<uint32_t> expected;
          for (unsigned i = 0; i < unsigned(fx.params[0]); ++i)
            if (tiled_expected(v, fx.lights[i], x, y))
              expected.push_back(i);
          require(data[view][offset] == expected.size(),
                  "Tiled GPU mask differs from analytical CPU reference");
          for (size_t i = 0; i < expected.size(); ++i)
            require(data[view][offset + 1 + i] == expected[i],
                    "Tiled GPU light order/index mismatch");
          if (expected.empty())
            ++empty;
          else
            ++populated;
        }
    }
    return std::array<unsigned, 2>{populated, empty};
  };
  const auto coverage = verify(views, lights);
  require(coverage[0] > 0 && coverage[1] > 0, "Tiled fixture failed to exercise actual culling");
  std::reverse(views.begin(), views.end());
  verify(views, lights);
  lights.params[0] = 64;
  for (auto &l : lights.lights)
    l = lights.lights[0];
  verify(views, lights);
  lights.params[0] = 0;
  verify(views, lights);
  verify(
      std::vector<tiled::View>{tiled::make_view(identity, {32, -32, 32, 24}, 1, 100, 5120, 2160)},
      lights);
  auto rejects = [&](const auto &cameras, const auto &fx) {
    bool failed = false;
    try {
      renderer.probe_tiled_lists(cameras, fx);
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed, "Malformed or over-budget tiled input was accepted");
  };
  auto malformed = base;
  malformed.extent[2]++;
  rejects(std::vector<tiled::View>{malformed}, lights);
  lights.params[0] = 65;
  rejects(views, lights);
  lights.params[0] = 1;
  lights.lights[0].start[0] = 1e30f;
  rejects(views, lights);
  lights.lights[0] = {{0, 0, 8, 1}, {0, 0, 8, 0}, {1, 1, 1, 1}};
  const auto large = tiled::make_view(identity, {32, -32, 32, 24}, 1, 100, 5120, 2160);
  rejects(std::vector<tiled::View>(13, large), lights);
  verify(std::vector<tiled::View>{base}, lights); // rejected growth did not poison reuse
  const auto empty = renderer.probe_tiled_lists({}, lights);
  require(empty.empty(), "Empty tiled view retained prior outputs");
  auto singular = identity;
  singular[0] = 0;
  bool failed = false;
  try {
    (void)tiled::inverse_affine(singular);
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, "Singular tiled model transform accepted");
  const std::array<float, 12> model{0, 0, 2, 13, 0, 3, 0, -7, -4, 0, 0, 19};
  const auto roundtrip = tiled::compose_affine(model, tiled::inverse_affine(model));
  for (unsigned i = 0; i < 12; ++i)
    require(std::abs(roundtrip[i] - identity[i]) < 1e-5f, "Tiled affine inverse/composition");
  std::fprintf(stderr, "Tiled light GPU: exact masks, 64-light ordering, isolated views, maximum "
                       "extent and rejection recovery passed\n");
  check_tiled_view_render();
}
