public:
void flush() override {
    if (scene_.order.empty()) {
        return;
    }
    if (!local_active_)
      local_lights_attach(scene_);
    CpuInterval interval(cpu_profile_ ? &render_ticks_ : nullptr);
    if (cpu_profile_)
        ++render_calls_;
    const bool initialize = scene_.width != resident_width_ || scene_.height != resident_height_;
    if (initialize) {
        const auto size = std::size_t(scene_.width) * scene_.height;
        scene_.color.assign(size, 0);
        scene_.depth.assign(size, 0);
    }
    // Validate before moving the scene or advancing upload/extent receipts.
    // A rejected submission retains all payloads for a consistent retry.
    std::size_t local_bytes = 0;
    if (local_active_) {
      local_bytes = packet_bytes(scene_);
      constexpr std::size_t kLocalBudget = 128u * 1024u * 1024u;
      if (local_bytes > kLocalBudget - pending_local_bytes_)
        throw std::runtime_error("Local light packet staging exceeds budget");
      if (worker_)
        for (const auto &draw : scene_.draws)
          if (draw.shadow_valid)
            throw std::runtime_error(
                "Render packet contains a live shadow callback");
    }
    resident_width_ = scene_.width;
    resident_height_ = scene_.height;
    // Track the last ordered upload, not the worker's mutable Vulkan cache.
    // Every omitted payload refers to an earlier packet on this same queue.
    for (const auto &texture : scene_.textures)
        submitted_textures_[texture.key] = {texture.hash, texture.width, texture.height};
    if (local_active_) {
      auto packet = std::make_shared<vk::Scene>(std::move(scene_));
      pending_local_packets_.push_back(
          PendingLocalPacket{std::move(packet), initialize, local_bytes});
      pending_local_bytes_ += local_bytes;
      scene_ = {};
      std::lock_guard<std::mutex> lock(recycle_mutex_);
      if (!recycled_scenes_.empty()) {
        scene_ = std::move(recycled_scenes_.back());
        recycled_scenes_.pop_back();
      }
      for (auto &entry : textures_)
        entry.scene_index = entry.sharp_index = no_binding;
      return;
    }
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
  struct PendingLocalPacket {
    std::shared_ptr<vk::Scene> packet;
    bool initialize;
    std::size_t bytes;
  };
  std::vector<PendingLocalPacket> pending_local_packets_;
  std::size_t pending_local_bytes_{};
  void complete_local_light_packets() {
    // Detach the batch before posting: an exception must not cause a later
    // drain/destructor to replay packets already submitted to the worker.
    auto packets = std::move(pending_local_packets_);
    pending_local_packets_.clear();
    pending_local_bytes_ = 0;
    for (const auto &pending : packets) {
      auto packet = pending.packet;
      const bool initialize = pending.initialize;
      local_lights_attach(*packet);
      if (worker_) {
        worker_->post(
            [this, packet, initialize] {
              {
                CpuInterval interval(cpu_profile_ ? &worker_render_ticks_
                                                  : nullptr);
                renderer_->render(*packet, initialize, false);
              }
              recycle_scene(*packet);
              std::lock_guard<std::mutex> lock(recycle_mutex_);
              recycled_scenes_.push_back(std::move(*packet));
            },
            pending.bytes);
      } else {
        renderer_->render(*packet, initialize, false);
        recycle_scene(*packet);
        std::lock_guard<std::mutex> lock(recycle_mutex_);
        recycled_scenes_.push_back(std::move(*packet));
      }
    }
    packets.clear();
    pending_local_packets_.swap(packets);
    if (scene_.order.empty()) {
      std::lock_guard<std::mutex> lock(recycle_mutex_);
      if (!recycled_scenes_.empty()) {
        scene_ = std::move(recycled_scenes_.back());
        recycled_scenes_.pop_back();
      }
    }
  }
static std::size_t packet_bytes(const vk::Scene &scene) {
  // sizeof(scene) includes the owned fixed local-light Uniform.
  std::size_t bytes =
      sizeof(scene) + scene.vertices.capacity() +
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