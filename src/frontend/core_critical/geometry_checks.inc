void check_strict_geometry(SDL_Window *window, sdlfe::RendererBackend backend) {
    sdlfe::CoreCriticalAssets options;
    options.geometry = "gpu";
    auto renderer = make_check_renderer(window, backend, {}, options);
    renderer->set_viewport(0, 0, 32, 32);
    renderer->begin_scene();
    renderer->clear(true, 0, 0, 0);
    for (bool overlay : {false, true}) {
        platform::DrawState state;
        state.depth_always = overlay;
        bool rejected = false;
        try {
            quad(*renderer, 4, 28, .5f, 0xffff0000, state);
        } catch (const std::runtime_error &error) {
            rejected = std::string_view(error.what()).find("Strict GPU geometry: CPU 3D draw") == 0;
        }
        require(rejected, "Strict mode silently admitted CPU geometry or a 3D overlay");
    }
    platform::DrawState hud;
    hud.sharp = hud.depth_always = true;
    hud.depth_write = false;
    quad(*renderer, 4, 28, 0, 0xff00ff00, hud);
    renderer->end_scene();
    std::vector<std::uint32_t> pixels;
    int width{}, height{};
    require(renderer->read_back(&pixels, &width, &height) && width == 32 && height == 32 &&
                pixels[16 * 32 + 16] == 0xff00ff00,
            "Strict mode lost the permitted output-resolution HUD");
    require(renderer->geometry_statistics().cpu_draw_calls == 2,
            "Residual CPU draw attempts were not reported");
}

std::vector<std::vector<std::uint32_t>> check_native_geometry(SDL_Window *window,
                                                              sdlfe::RendererBackend backend,
                                                              bool cpu_reference = false) {
    sdlfe::CoreCriticalAssets options;
    options.geometry = cpu_reference ? "cpu-reference" : "partial";
    auto renderer = make_check_renderer(window, backend, {}, options);
    renderer->set_viewport(0, 0, 32, 32);
    std::array<float, 12> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    platform::ModelDrawContext context;
    context.model_to_camera = context.model_to_world = matrix.data();
    context.ordinary = true;
    context.near_z = context.clip_near = 1;
    context.far_z = 10;
    context.fx_near = context.fy_near = 16;
    context.cx = context.cy = 16;
    float positions[]{-1, -1, 3, 1, -1, 3, 0, 1, 3};
    std::uint32_t indices[]{2, 1, 0};
    bool two_sided = false;
    const auto make = [&] {
        return cc::geometry::register_resource(positions, 3, nullptr, 0,
                                               {{3, indices, nullptr, nullptr, false, two_sided}});
    };
    auto source = make();
    cc::geometry::Material material;
    material.color = 0xffff0000;
    const auto frame = [&](const cc::geometry::Fog &fog = cc::geometry::Fog{}) {
        renderer->begin_scene();
        renderer->clear(true, 0, 0, 0);
        require(renderer->submit_model_geometry(source, context, {material}, fog) ==
                    cc::geometry::Submission::submitted,
                "Authored native geometry rejected");
        renderer->end_scene();
        std::vector<std::uint32_t> pixels;
        int width{}, height{};
        require(renderer->read_back(&pixels, &width, &height) && width == 32 && height == 32,
                "Native geometry readback changed dimensions");
        return pixels;
    };
    const auto first = frame();
    require(std::count_if(first.begin(), first.end(),
                          [](auto p) { return (p & 0x00ff0000) > 0x00800000; }) > 10,
            "Indexed native GPU geometry produced no red pixels");
    require(frame() == first &&
                renderer->asset_statistics().geometry_uploads == (cpu_reference ? 0 : 1),
            "Immutable native geometry changed or reuploaded every frame");
    matrix[9] = .75f;
    const auto moved = frame();
    require(moved != first &&
                renderer->asset_statistics().geometry_uploads == (cpu_reference ? 0 : 1),
            "GPU instance transform was ignored or rebuilt vertices");
    matrix[9] = 0;
    cc::geometry::Fog fog;
    fog.enabled = true;
    fog.inverse_range = 1;
    fog.low_altitude = 10;
    fog.inverse_altitude = 1;
    fog.color = {0, 0, 1};
    const auto fogged = frame(fog);
    require(std::count_if(fogged.begin(), fogged.end(), [](auto p) { return (p & 255) > 128; }) >
                10,
            "Vertex fog was not evaluated on the GPU");
    positions[2] = .25f;
    source = make();
    const auto clipped = frame();
    require(std::count_if(clipped.begin(), clipped.end(),
                          [](auto p) { return (p & 0x00ff0000) > 0x00800000; }) > 0,
            "Near-plane crossing was dropped instead of GPU-clipped");
    context.clip_near = 4;
    const auto near_rejected = frame();
    require(std::none_of(near_rejected.begin(), near_rejected.end(),
                         [](auto p) { return (p & 0x00ffffff) != 0; }),
            "View near plane distinct from projection near scale was ignored");
    context.clip_near = 1;
    positions[2] = positions[5] = positions[8] = 20;
    source = make();
    const auto far = frame();
    require(std::none_of(far.begin(), far.end(), [](auto p) { return (p & 0x00ffffff) != 0; }),
            "Hardware far clipping was omitted");
    const auto statistics = renderer->geometry_statistics();
    require(statistics.submitted_models == 7 && !statistics.unsupported_models &&
                (cpu_reference ? renderer->asset_statistics().cpu_clipped_triangles > 0
                               : renderer->asset_statistics().cpu_clipped_triangles == 0),
            "Native indexed geometry used an unreported CPU route");
    if (!cpu_reference)
        require(renderer->asset_statistics().retired_geometry_buffers >= 2,
                "Expired native GPU resources were not retired after completion");
    renderer.reset();
    options.geometry_debug_normals = true;
    renderer = make_check_renderer(window, backend, {}, options);
    renderer->set_viewport(0, 0, 32, 32);
    positions[2] = positions[5] = positions[8] = 3;
    two_sided = true;
    source = make();
    const auto normal_debug = frame();
    require(normal_debug != first &&
                std::count_if(normal_debug.begin(), normal_debug.end(),
                              [](auto p) { return ((p >> 8) & 255) > 100; }) > 10,
            "GPU normal debug view was not evaluated");
    const auto front_normal = normal_debug[16 * 32 + 16];
    require(((front_normal >> 16) & 255) > 100 && ((front_normal >> 8) & 255) > 100 &&
                (front_normal & 255) < 10,
            "Two-sided front normal points away from the camera");
    std::swap(indices[0], indices[1]);
    source = make();
    const auto back_normal = frame();
    require(back_normal == normal_debug, "Two-sided back normal did not face the viewer");
    std::printf("Native %s transforms, fog and near/far clipping pass\n",
                cpu_reference ? "CPU reference" : "indexed GPU");
    return {first, moved, fogged, clipped, near_rejected, far, normal_debug, back_normal};
}
