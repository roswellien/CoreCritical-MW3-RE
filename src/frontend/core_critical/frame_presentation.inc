void present() override {
  flush();
  complete_local_light_packets();
  if (!resident_width_) {
    return;
  }
  if (!capture_path_.empty()) {
    save_capture();
    return;
  }
  const auto window_state =
      window_state_tracker_
          ? window_state_tracker_->sample_and_get()
          : cc_mw3_re::platform::SdlWindowState{
                (SDL_GetWindowFlags(window_) &
                 (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0,
                0, 0};
  if (!window_state.visible) {
    return;
  }
  CpuInterval interval(cpu_profile_ ? &transport_ticks_ : nullptr);
  if (!window_bound_) {
    std::vector<uint32_t> argb;
    int width = 0, height = 0;
    if (!read_back(&argb, &width, &height)) {
      throw std::runtime_error("First Vulkan frame could not be read back");
    }
    const auto rgba = rgba_copy(argb);
    // Window/surface creation stays on the SDL window owner. Readback
    // above is a queue barrier; the worker cannot race this cold path.
    const auto before = renderer_->presents;
    if (!renderer_->present(native_window_,
                            reinterpret_cast<const uint8_t *>(rgba.data()),
                            width, height, width * 4, false, true)) {
      throw std::runtime_error(
          "Could not bind the selected Core Critical Vulkan presenter");
    }
    window_bound_ = renderer_->presents != before;
    presented_frames_ += renderer_->presents - before;
    if (window_bound_ && !presented_) {
      std::fprintf(stderr, "%s first frame: %s (upload)\n", driver_name(),
                   renderer_->device_name.c_str());
      sdlfe::mark_ready();
    }
    presented_ = window_bound_;
  } else {
    int window_width = 0, window_height = 0;
    if (window_state_tracker_) {
      window_width = window_state.drawable_width;
      window_height = window_state.drawable_height;
    } else {
      SDL_GetWindowSizeInPixels(window_, &window_width, &window_height);
    }
    if (window_width != present_window_width_ ||
        window_height != present_window_height_) {
      if (worker_)
        worker_->drain();
      renderer_->resize_present_window(native_window_);
      present_window_width_ = window_width;
      present_window_height_ = window_height;
    }
    const auto present = [this, width = resident_width_,
                          height = resident_height_, window_state] {
      CpuInterval interval(worker_ && cpu_profile_ ? &worker_present_ticks_
                                                   : nullptr);
      cc_mw3_re::platform::SdlWindowStateScope job_scope(
          window_state_tracker_.get(), window_state);
      const auto before = renderer_->presents;
      if (!renderer_->present_resident(native_window_, width, height, false))
        throw std::runtime_error("Core Critical resident present failed");
      presented_frames_ += renderer_->presents - before;
      if (renderer_->resident_presents)
        resident_draws_.store(renderer_->draws);
    };
    if (worker_)
      worker_->post(present);
    else
      present();
  }
  if (!reported_resident_ && resident_draws_.load()) {
    std::fprintf(stderr, "%s resident frame: %llu Vulkan draws\n",
                 driver_name(),
                 static_cast<unsigned long long>(resident_draws_.load()));
    reported_resident_ = true;
  }
}
