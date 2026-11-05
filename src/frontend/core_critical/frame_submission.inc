public:
void flush() override {
    if (scene_.order.empty()) {
        return;
    }
    CpuInterval interval(cpu_profile_ ? &render_ticks_ : nullptr);
    if (cpu_profile_)
        ++render_calls_;
    const bool initialize = scene_.width != resident_width_ || scene_.height != resident_height_;
    if (initialize) {
        const auto size = std::size_t(scene_.width) * scene_.height;
        scene_.color.assign(size, 0);
        scene_.depth.assign(size, 0);
    }
    resident_width_ = scene_.width;
    resident_height_ = scene_.height;
    // Track the last ordered upload, not the worker's mutable Vulkan cache.
    // Every omitted payload refers to an earlier packet on this same queue.
    for (const auto &texture : scene_.textures)
        submitted_textures_[texture.key] = {texture.hash, texture.width, texture.height};
    if (worker_) {
        for (const auto &draw : scene_.draws)
            if (draw.shadow_valid)
                throw std::runtime_error("Render packet contains a live shadow callback");
        auto packet = std::make_shared<vk::Scene>(std::move(scene_));
        const auto bytes = packet_bytes(*packet);
        worker_->post(
            [this, packet, initialize] {
                {
                    CpuInterval interval(cpu_profile_ ? &worker_render_ticks_ : nullptr);
                    renderer_->render(*packet, initialize, false);
                }
                recycle_scene(*packet);
                std::lock_guard<std::mutex> lock(recycle_mutex_);
                recycled_scenes_.push_back(std::move(*packet));
            },
            bytes);
        scene_ = {};
        std::lock_guard<std::mutex> lock(recycle_mutex_);
        if (!recycled_scenes_.empty()) {
            scene_ = std::move(recycled_scenes_.back());
            recycled_scenes_.pop_back();
        }
    } else {
        renderer_->render(scene_, initialize, false);
        recycle_scene(scene_);
    }
    for (auto &entry : textures_)
        entry.scene_index = entry.sharp_index = no_binding;
}

private:
static std::size_t packet_bytes(const vk::Scene &scene) {
    std::size_t bytes = sizeof(scene) + scene.vertices.capacity() +
                        (scene.color.capacity() + scene.depth.capacity()) * sizeof(uint16_t) +
                        scene.draws.capacity() * sizeof(vk::Draw) +
                        scene.order.capacity() * sizeof(vk::Command) +
                        scene.textures.capacity() * sizeof(vk::TextureRef);
    for (const auto &texture : scene.textures)
        bytes += texture.pixels.capacity() * sizeof(uint16_t) +
                 texture.pixels32.capacity() * sizeof(uint32_t);
    for (const auto &draw : scene.draws) {
        if (draw.model && draw.model->geometry) {
            const auto &geometry = *draw.model->geometry;
            bytes += geometry.vertices.capacity() * sizeof(vk::ModelVertex) +
                     geometry.indices.capacity() * sizeof(uint32_t);
        }
        if (draw.lit_normals)
            bytes += draw.lit_normals->capacity() * sizeof(std::array<float, 3>);
    }
    for (const auto &clear : scene.clears)
        bytes += clear.rects.capacity() * sizeof(VkRect2D);
    return bytes;
}
static void recycle_scene(vk::Scene &scene) {
    // Reuse hot submission storage, like the SDL renderer. Reset every
    // scene value and release its resource references while keeping capacity.
    vk::Scene next{};
    next.vertices.swap(scene.vertices);
    next.vertices.clear();
    next.draws.swap(scene.draws);
    next.draws.clear();
    next.textures.swap(scene.textures);
    next.textures.clear();
    next.order.swap(scene.order);
    next.order.clear();
    next.clears.swap(scene.clears);
    next.clears.clear();
    scene = std::move(next);
}

public:
