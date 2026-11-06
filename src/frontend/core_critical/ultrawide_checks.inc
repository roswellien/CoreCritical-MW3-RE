void check_ultrawide(SDL_Window *window, sdlfe::RendererBackend backend) {
    sdlfe::CoreCriticalAssets options;
    options.geometry = "partial";
    sdlfe::FrameShape shape;
    auto renderer = make_check_renderer(window, backend, {}, options);
    shape.fit = true;
    renderer->set_shape(&shape);
    const std::uint8_t yellow[]{255, 255, 0, 255}, cyan[]{0, 255, 255, 255};
    const auto left_texture = renderer->create_texture(1, 1, yellow, 1);
    const auto right_texture = renderer->create_texture(1, 1, cyan, 1);
    float positions[]{-3, -1, 3, 3, -1, 3, 0, 1, 3};
    std::uint32_t indices[]{2, 1, 0};
    const auto source = cc::geometry::register_resource(
        positions, 3, nullptr, 0, {{3, indices, nullptr, nullptr, false, true}});
    cc::geometry::Material material;
    material.color = 0xffff0000;
    const std::array<float, 12> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    int previous_span = 0;
    // One tenth physical size keeps this asset-free fixture inexpensive. Native
    // captures separately verify full 3440/5120 output. Equal height must keep
    // a world object's pixel width constant as the horizontal view expands.
    for (int native_width : {3440, 5120, 2560, 5120}) {
        renderer->drain();
        const int width = native_width / 10, height = 144;
        require(SDL_SetWindowSize(window, width, height) && SDL_SyncWindow(window), SDL_GetError());
        shape.frame_w = native_width;
        shape.frame_h = 1440;
        shape.canvas_w = (native_width + 1) / 3;
        shape.canvas_h = 480;
        if (native_width == 2560)
            renderer->reset_scene_resources();
        renderer->set_viewport(0, 0, native_width, 1440);
        if (previous_span) {
            require(!renderer->restore_frame(), "A held frame survived an incompatible extent");
            renderer->drop_frame();
        }
        renderer->clear(true, 0, 0, 255);
        platform::ModelDrawContext context;
        context.model_to_camera = context.model_to_world = matrix.data();
        context.ordinary = true;
        context.near_z = context.clip_near = 1;
        context.far_z = 10;
        context.fx_near = context.fy_near = 720;
        context.cx = native_width * .5f;
        context.cy = 720;
        require(renderer->submit_model_geometry(source, context, {material}, {}) ==
                    cc::geometry::Submission::submitted,
                "Ultrawide model was not submitted");
        renderer->set_viewport(0, 0, shape.canvas_w, shape.canvas_h);
        const auto edge = [&](float left, float right, std::int32_t texture) {
            const platform::TLVertex vertices[] = {{left, 0, 0, 1, 0xffffffff, 0, 0, 0},
                                                   {right, 0, 0, 1, 0xffffffff, 0, 1, 0},
                                                   {right, 33, 0, 1, 0xffffffff, 0, 1, 1},
                                                   {left, 33, 0, 1, 0xffffffff, 0, 0, 1}};
            platform::DrawState hud;
            hud.texture = texture;
            hud.sharp = hud.depth_always = true;
            hud.depth_write = false;
            renderer->draw(platform::Primitive::kFan, vertices, 4, hud);
        };
        edge(0, 33, left_texture);
        edge(float(shape.canvas_w - 33), float(shape.canvas_w), right_texture);
        renderer->set_viewport(0, 0, native_width, 1440);
        std::vector<std::uint32_t> pixels;
        int output_w = 0, output_h = 0;
        require(renderer->read_back(&pixels, &output_w, &output_h) && output_w == width &&
                    output_h == height,
                "Ultrawide readback is cropped or has the wrong extent");
        require(pixels[2 * width + 2] == 0xffffff00 && pixels[2 * width + width - 3] == 0xff00ffff,
                "Ultrawide HUD edges do not cover the full frame");
        int span = 0;
        for (int x = 0; x < width; ++x)
            span += (pixels[(height / 2) * width + x] & 0xffffff) == 0xff0000;
        require(span > 0 && (!previous_span || std::abs(span - previous_span) <= 1),
                "Ultrawide projection stretches world geometry instead of expanding coverage");
        previous_span = span;
        require(renderer->hold_frame(), "Ultrawide frame could not be held");
        renderer->clear(true, 0, 255, 0);
        require(renderer->restore_frame(), "Ultrawide held frame could not be restored");
        std::vector<std::uint32_t> restored;
        require(renderer->read_back(&restored, &output_w, &output_h) && restored == pixels,
                "Ultrawide held frame changed after resize/reset");
        SDL_ShowWindow(window);
        renderer->present();
        renderer->drain();
    }
    renderer->drop_frame();
    renderer->release_texture(left_texture);
    renderer->release_texture(right_texture);
    require(renderer->presentation_count() > 0, "Ultrawide fixture never presented");
}
