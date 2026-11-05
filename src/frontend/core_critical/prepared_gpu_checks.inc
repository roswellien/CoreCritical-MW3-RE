void check_gpu_prepared(SDL_Window *window, sdlfe::RendererBackend backend) {
    const auto folder = std::filesystem::temp_directory_path() /
                        ("cc-gpu-prepared-" + std::to_string(SDL_GetPerformanceCounter()));
    require(std::filesystem::create_directory(folder), "GPU fixture directory already exists");
    struct Files {
        std::filesystem::path folder;
        ~Files() {
            std::error_code error;
            for (const char *name : {"mesh.bin", "textures.bin", "normals.bin"})
                std::filesystem::remove(folder / name, error);
            std::filesystem::remove(folder, error);
        }
    } files{folder};
    std::ofstream(folder / "mesh.bin", std::ios::binary) << prepared_fixture::mesh(2);
    std::ofstream(folder / "textures.bin", std::ios::binary) << prepared_fixture::textures();
    std::ofstream(folder / "normals.bin", std::ios::binary)
        << prepared_fixture::normals(0xffd980d9);
    std::unique_ptr<zcls::Node, int (*)(zcls::Node *)> root(zcls::object3d_create(),
                                                            zcls::tree_destroy);
    auto *control = zcls::object3d_create(), *panel = zcls::object3d_create();
    require(root && control && panel && zcls::object3d_add_child(root.get(), control) == 0 &&
                zcls::object3d_add_child(control, panel) == 0,
            "GPU fixture hierarchy failed");
    zcls::node_set_name(root.get(), "actor");
    zcls::node_set_name(control, "control");
    zcls::node_set_name(panel, "panel");
    auto asset = std::make_shared<zcls::SourceAsset>();
    asset->name = "mech_authored.flt";
    asset->nodes = {{"actor", 1}, {"control", 1}, {"panel", 1}};
    root->source_asset = control->source_asset = panel->source_asset = asset;
    root->source_asset_root = true;
    zmod::Model model{};
    const std::uint32_t count = 3;
    std::memcpy(model.header + 0x14, &count, sizeof count);
    panel->model = &model;
    panel->source_model_vertices = count;
    struct ModelPointer {
        zcls::Node *node;
        ~ModelPointer() { node->model = nullptr; }
    } model_pointer{panel};
    require(SDL_SetWindowSize(window, 64, 64) && SDL_SyncWindow(window), SDL_GetError());
    std::array<float, 12> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    platform::ModelDrawContext context;
    context.node = panel;
    context.ordinary = true;
    context.source_vertices = 3;
    context.source_polygons = 1;
    context.model_to_camera = context.model_to_world = matrix.data();
    context.near_z = context.clip_near = 1;
    context.far_z = 5;
    context.fx_near = context.fy_near = 20;
    context.cx = context.cy = 32;
    std::vector<std::vector<std::uint32_t>> expected;
    for (const char *mode : {"cpu-reference", "partial"}) {
        sdlfe::CoreCriticalAssets options;
        options.geometry = mode;
        options.models.push_back({"authored", "0", (folder / "mesh.bin").u8string(),
                                  (folder / "textures.bin").u8string()});
        options.normals.push_back({"authored", "0", (folder / "normals.bin").u8string()});
        options.lighting = "normal";
        auto renderer = make_check_renderer(window, backend, {}, options);
        renderer->set_viewport(0, 0, 64, 64);
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            matrix[2] = scenario == 5 ? 1.5f : 0;
            matrix[9] = scenario == 1 ? .5f : 0;
            matrix[11] = scenario == 3 ? 8.f : 0;
            context.clip_near = scenario == 4 ? 3.f : 1.f;
            cc::geometry::Fog fog;
            fog.enabled = scenario == 2;
            fog.color = {0, 0, 1};
            fog.near_range = 1;
            fog.inverse_range = .2f;
            fog.low_altitude = 2;
            fog.inverse_altitude = .2f;
            renderer->clear(true, 0, 0, 0);
            require(renderer->submit_prepared_geometry(context, fog) ==
                        cc::geometry::Submission::submitted,
                    "Rigid prepared model was not admitted at the early handoff");
            std::vector<std::uint32_t> pixels;
            int width{}, height{};
            require(renderer->read_back(&pixels, &width, &height) && width == 64 && height == 64,
                    "GPU prepared packet lost its extent");
            if (expected.size() < 6)
                expected.push_back(pixels);
            else {
                require(pixels.size() == expected[scenario].size(),
                        "GPU prepared comparison lost pixels");
                for (std::size_t index = 0; index < pixels.size(); ++index)
                    for (unsigned shift : {0U, 8U, 16U})
                        require(
                            std::abs(int((pixels[index] >> shift) & 255) -
                                     int((expected[scenario][index] >> shift) & 255)) <= 2,
                            "GPU prepared transform/fog/normal/clipping differs from reference");
            }
        }
        const auto statistics = renderer->asset_statistics();
        require(statistics.early_prepared_models == 6 && !statistics.prepared_cpu_fallback_batches,
                "Early prepared submission used an unreported CPU fallback");
        if (std::string_view(mode) == "partial")
            require(
                statistics.gpu_prepared_vertices == 18 && statistics.geometry_uploads == 1 &&
                    statistics.cpu_clipped_triangles == 0 && statistics.normal_draws == 6,
                "GPU prepared vertices still use CPU clipping or lost immutable material draws");
    }
    require(expected[0] != expected[1] && expected[0] != expected[2],
            "Prepared reference did not exercise motion or fog");
    for (unsigned scenario : {3U, 4U})
        require(std::all_of(expected[scenario].begin(), expected[scenario].end(),
                            [](auto pixel) { return (pixel & 0xffffff) == 0; }),
                "Prepared depth rejection did not remove geometry");
}
