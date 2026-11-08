// Included in the authenticated native fixture: the same source identity is
// exercised without a normal catalog, through immediate and real queued draws.
{
  struct NativeOnlyGuard {
    zmth::Projection projection;
    zmth::View view;
    zvid::DeviceState device_state;
    platform::DrawState draw_state;
    zvid::D3DTexture &texture;
    int handle;
    std::string world;
    ~NativeOnlyGuard() {
      zmth::projection() = projection;
      zmth::view() = view;
      zvid::device_state() = device_state;
      zvid::draw_state() = draw_state;
      texture.handle = handle;
#if defined(_WIN32)
      _putenv_s("MW34_WORLD_NORMALS", world.c_str());
      _putenv_s("MW34_SURFACE_NORMALS", "");
#else
      setenv("MW34_WORLD_NORMALS", world.c_str(), 1);
      unsetenv("MW34_SURFACE_NORMALS");
#endif
    }
  } guard{zmth::projection(), zmth::view(),   zvid::device_state(),
          zvid::draw_state(), device_texture, device_texture.handle,
          world_path.string()};
#if defined(_WIN32)
  _putenv_s("MW34_WORLD_NORMALS", "");
  _putenv_s("MW34_SURFACE_NORMALS", "");
#else
  unsetenv("MW34_WORLD_NORMALS");
  unsetenv("MW34_SURFACE_NORMALS");
#endif
  zmth::projection() = {};
  auto &projection = zmth::projection();
  projection.near_z = 1;
  projection.far_z = 100;
  projection.fx_near = projection.fy_near = 32;
  projection.cx = projection.cy = 32;
  zmth::view().camera = {{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
  const mw34::materials::SurfaceKey material_key{128, 128, 1,
                                                 0x123456789abcULL};
  auto run = [&](float roughness, bool bound = true) {
    const auto bytes = surface_metadata(
        "native:fixture", "ggter1", *native_hash, roughness, .04f, 1.f, bound);
    std::ofstream(metadata_path, std::ios::binary)
        .write(bytes.data(), bytes.size());
    sdlfe::CoreCriticalAssets isolated;
    isolated.lighting = "normal";
    isolated.sun_azimuth = "-90";
    isolated.sun_elevation = "1";
    auto active = make_check_renderer(window, backend, {}, isolated);
    struct RendererGuard {
      platform::Render3D *previous;
      ~RendererGuard() { zvid::set_render3d(previous); }
    } renderer_guard{checked_renderer.get()};
    active->set_viewport(0, 0, 64, 64);
    mw34::materials::set_active_surface_key(material_key);
    device_texture.handle =
        active->create_named_texture("ggter1", 128, 128, green_data.data(), 1);
    mw34::materials::clear_active_surface_key();
    zvid::set_render3d(active.get());
    zvid::draw_buffers().tl =
        reinterpret_cast<zvid::TLVertex *>(deferred_vertices);
    zvid::device_state() = {};
    zvid::device_state().z_write = 1;
    zvid::draw_state() = {};
    auto read = [&] {
      std::vector<uint32_t> result;
      int w{}, h{};
      require(active->read_back(&result, &w, &h) && w == 64 && h == 64,
              "Material-only native readback failed");
      require(active->asset_statistics().normal_draws == 0,
              "Material-only fixture unexpectedly bound a normal map");
      return result;
    };
    auto submit = [&](int overlay) {
      active->begin_model_draw(model_context);
      active->begin_material_draw(material_context);
      zvid::draw_textured_white(3, &device_texture, 0, 1.f, overlay);
      active->end_material_draw();
      active->end_model_draw();
    };
    active->clear(true, 0, 0, 0);
    submit(0);
    const auto immediate = read();
    active->clear(true, 0, 0, 0);
    submit(1);
    zvid::flush_overlay();
    const auto queued = read();
    if (bound)
      require(queued == immediate,
              "Material-only opaque queue lost authenticated response");
    // Invalidate an actual queued receipt between admission and consumption.
    active->clear(true, 0, 0, 0);
    submit(1);
    const auto rejected = active->asset_statistics().binding_rejected_mismatch;
    mw34::materials::set_active_surface_key(material_key);
    active->update_texture(device_texture.handle, 128, 128, green_data.data());
    mw34::materials::clear_active_surface_key();
    zvid::flush_overlay();
    const auto stale = read();
    require(active->asset_statistics().binding_rejected_mismatch > rejected,
            "Material-only queue retained a stale upload receipt");
    return std::array<std::vector<uint32_t>, 3>{immediate, queued, stale};
  };
  const auto smooth = run(.1f);
  const auto rough = run(1.f);
  const auto legacy = run(.7f, false);
  require(smooth[0] != rough[0] && smooth[1] != rough[1],
          "Material-only roughness did not affect immediate and queued draws");
  // An unqualified opaque overlay keeps its original HUD/overlay path.
  require(smooth[2] == legacy[1] && rough[2] == legacy[1] &&
              smooth[1] != legacy[1],
          "Stale material-only receipt did not restore legacy overlay output");
}
