// Private Renderer implementation. The alternate pair owns the inactive target;
// swapping ownership lets the existing draw/scissor/guest-transfer paths use
// rw/rh.
Image worldColor, worldDepth;
VkFramebuffer worldFramebuffer{};
unsigned alternate_width{}, alternate_height{};
bool reduced_world_active{};
bool retained_color_warning{};
enum class ReconstructionStage { normal, shading, coverage };
ReconstructionStage reconstruction_stage{ReconstructionStage::normal};
bool world_scene_prepared{};
const Scene *world_shadow_scene{};
bool world_shadow_inputs_ready{};
bool world_shadow_recorded{};
ShadowFrame world_shadow_frame{};
mw34::effects::Uniform world_fixture_lights{};
Image reconstructedColor;
VkFramebuffer reconstructionFramebuffer{};
VkShaderModule coverageFrag{};

// Only opaque resident submissions can replace their shaded RGB during replay.
// Blended draws and guest uploads keep the existing ordered full-resolution
// path.
bool can_reconstruct_world(const Scene &scene, bool upload,
                           bool readback) const {
  return worldColor.image && !scene.hud && !scene.guest_surface_write &&
         !scene.frontend_canvas && !upload && !readback &&
         !scene.draws.empty() && !(leading_full_clear(scene) & 1) &&
         std::any_of(scene.order.begin(), scene.order.end(),
                     [&](const Command &command) {
                       if (command.clear)
                         return false;
                       const auto &draw = scene.draws.at(command.index);
                       return !draw.key.blend && !draw.key.points;
                     });
}
// Keep the guest command stream ordered. Only contiguous opaque runs use the
// reconstruction path; intervening clears, fades and transparent draws retain
// their original order and full-resolution destination reads.
void render_reconstructed_world(Scene &scene) {
  // Sky routing can use a later draw's camera. Prepare the complete original
  // scene once before splitting, then share that prepared state in both passes.
  apply_atmosphere(scene, lighting_settings);
  struct PreparedScene {
    Renderer &renderer;
    explicit PreparedScene(Renderer &owner, const Scene &source)
        : renderer(owner) {
      renderer.world_scene_prepared = true;
      renderer.world_shadow_scene = &source;
      renderer.world_shadow_inputs_ready = false;
      renderer.world_shadow_recorded = false;
      renderer.world_shadow_frame = {};
      renderer.world_fixture_lights = {};
    }
    ~PreparedScene() {
      renderer.world_scene_prepared = false;
      renderer.world_shadow_scene = nullptr;
      renderer.world_shadow_inputs_ready = false;
      renderer.world_shadow_recorded = false;
      renderer.world_shadow_frame = {};
      renderer.world_fixture_lights = {};
    }
  } prepared(*this, scene);
  Scene batch{};
  batch.detail_time = scene.detail_time;
  batch.world_frame = scene.world_frame;
  batch.world_generation = scene.world_generation;
  batch.world_context = scene.world_context;
  batch.world_eye = scene.world_eye;
  batch.world_eye_valid = scene.world_eye_valid;
  batch.effect_lights = scene.effect_lights;
  batch.width = scene.width;
  batch.height = scene.height;
  batch.has_depth = scene.has_depth;
  batch.model_warmup = scene.model_warmup;
  // Load every model before shadow preparation so later batches can cast into
  // the same full-scene maps as the first batch.
  for (const auto &draw : scene.draws)
    if (draw.model)
      batch.model_warmup.push_back(draw.model->geometry);
  // Geometry, immutable texture inputs and emission indices have one owner.
  // Move their containers for batching, then restore them even on exceptions.
  struct ResidentInputs {
    Scene &source;
    Scene &batch;
    ResidentInputs(Scene &source_scene, Scene &batch_scene)
        : source(source_scene), batch(batch_scene) {
      swap();
    }
    void swap() {
      source.vertices.swap(batch.vertices);
      source.textures.swap(batch.textures);
      std::swap(source.emissions, batch.emissions);
    }
    ~ResidentInputs() { swap(); }
  } inputs(scene, batch);
  const auto original_scene_count = scenes;
  auto prepared_draws = scene.draws;
  std::vector<unsigned> original_indices;
  bool opaque = false, has_draw = false;
  auto flush = [&] {
    if (batch.order.empty())
      return;
    if (opaque && has_draw && !(leading_full_clear(batch) & 1))
      render_opaque_world_batch(batch);
    else
      render_scene(batch, false, false);
    for (size_t index = 0; index < original_indices.size(); ++index)
      prepared_draws[original_indices[index]] = batch.draws[index];
    batch.order.clear();
    batch.draws.clear();
    batch.clears.clear();
    original_indices.clear();
    has_draw = false;
  };
  for (const auto &command : scene.order) {
    if (command.clear) {
      batch.order.push_back({true, unsigned(batch.clears.size())});
      batch.clears.push_back(scene.clears.at(command.index));
    } else {
      const auto &draw = scene.draws.at(command.index);
      const bool next_opaque = !draw.key.blend && !draw.key.points;
      if (has_draw && opaque != next_opaque)
        flush();
      opaque = next_opaque;
      has_draw = true;
      batch.order.push_back({false, unsigned(batch.draws.size())});
      batch.draws.push_back(draw);
      original_indices.push_back(command.index);
    }
  }
  flush();
  scene.draws.swap(prepared_draws);
  // Batching and coverage replay are GPU work, not additional guest scenes.
  scenes = original_scene_count + 1;
}
void render_opaque_world_batch(Scene &scene) {
  finish_world();
  struct ResetStage {
    Renderer &renderer;
    ~ResetStage() {
      renderer.reconstruction_stage = ReconstructionStage::normal;
    }
  } reset{*this};
  reconstruction_stage = ReconstructionStage::shading;
  select_world_target(true, false);
  render_scene(scene, false, false);
  settle();
  reconstruct_world_color();
  reconstruction_stage = ReconstructionStage::coverage;
  render_scene(scene, false, false);
  quality_world_dirty = false;
  ++coverage_scenes;
}
void reconstruct_world_color() {
  check(f.vkResetCommandPool(device, commandPool, 0),
        "reset reconstruction pool");
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(f.vkBeginCommandBuffer(cmd, &begin), "begin reconstruction");
  barrier(colorImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  barrier(depthImage.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
  const auto shaded_image = colorImage.image;
  const auto shaded_width = rw, shaded_height = rh;
  // No promotion copy: keep the old output color and precise output depth.
  // The original geometry/depth operations will update those during replay.
  select_world_target(false, true);
  barrier(reconstructedColor.image, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT);
  blit_img(shaded_image, shaded_width, shaded_height, reconstructedColor.image,
           rw, rh, VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_LINEAR);
  barrier(reconstructedColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  if (fxaa_enabled()) {
    std::swap(colorImage, reconstructedColor);
    std::swap(framebuffer, reconstructionFramebuffer);
    quality_world_dirty = true;
    record_fxaa();
    barrier(depthImage.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    std::swap(colorImage, reconstructedColor);
    std::swap(framebuffer, reconstructionFramebuffer);
  }
  barrier(reconstructedColor.image,
          fxaa_enabled() ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                         : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          fxaa_enabled() ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                         : VK_ACCESS_TRANSFER_READ_BIT,
          VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  in_attachment_layout = false;
  quality_world_dirty = false;
  check(f.vkEndCommandBuffer(cmd), "end reconstruction");
  check(f.vkResetFences(device, 1, &fence), "reset reconstruction fence");
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  check(f.vkQueueSubmit(queue, 1, &submit, fence), "submit reconstruction");
  scene_pending = true;
}
unsigned world_scale_percent() const {
  const unsigned percent = bridge_config.world_scale_percent;
  if (percent < 50 || percent > 100)
    throw std::runtime_error(
        "MW34_WORLD_SCALE_PERCENT requires an integer from 50 to 100");
  return percent;
}
// Full color replacement is required before reducing a retained output image.
static unsigned leading_full_clear(const Scene &scene) {
  unsigned cleared = 0;
  for (const auto &command : scene.order) {
    if (!command.clear)
      break;
    const auto &clear = scene.clears.at(command.index);
    for (const auto &rect : clear.rects) {
      if (rect.offset.x == 0 && rect.offset.y == 0 &&
          rect.extent.width == scene.width &&
          rect.extent.height == scene.height)
        cleared |= clear.flags;
    }
  }
  return scene.has_depth ? cleared : 0;
}
void destroy_world_target() {
  if (reconstructionFramebuffer)
    f.vkDestroyFramebuffer(device, reconstructionFramebuffer, nullptr);
  reconstructionFramebuffer = {};
  if (reconstructedColor.view)
    f.vkDestroyImageView(device, reconstructedColor.view, nullptr);
  if (reconstructedColor.image)
    f.vkDestroyImage(device, reconstructedColor.image, nullptr);
  if (reconstructedColor.memory)
    f.vkFreeMemory(device, reconstructedColor.memory, nullptr);
  reconstructedColor = {};
  if (worldFramebuffer)
    f.vkDestroyFramebuffer(device, worldFramebuffer, nullptr);
  worldFramebuffer = {};
  for (Image *image : {&worldColor, &worldDepth}) {
    if (image->view)
      f.vkDestroyImageView(device, image->view, nullptr);
    if (image->image)
      f.vkDestroyImage(device, image->image, nullptr);
    if (image->memory)
      f.vkFreeMemory(device, image->memory, nullptr);
    *image = {};
  }
  reduced_world_active = false;
}
void create_world_target() {
  const auto percent = world_scale_percent();
  if (percent == 100 || target_frontend)
    return;
  const auto samples = sample_grid();
  if (samples.x != 1 || samples.y != 1)
    throw std::runtime_error(
        "Reduced world resolution requires AA off or FXAA, not SSAA");
  if (!remaster_width())
    throw std::runtime_error(
        "Reduced world resolution requires an explicit output resolution");
  alternate_width = std::max(1U, unsigned(std::uint64_t(rw) * percent / 100));
  alternate_height = std::max(1U, unsigned(std::uint64_t(rh) * percent / 100));
  const auto color_format =
      rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;
  for (auto format : {color_format, VK_FORMAT_D32_SFLOAT}) {
    VkFormatProperties properties{};
    f.vkGetPhysicalDeviceFormatProperties(gpu, format, &properties);
    auto required =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if (format == color_format)
      required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
      throw std::runtime_error("Reduced world resolution needs color/depth "
                               "blits and linear color filtering");
  }
  image(worldColor.image, worldColor.memory, worldColor.view, alternate_width,
        alternate_height, color_format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
  image(worldDepth.image, worldDepth.memory, worldDepth.view, alternate_width,
        alternate_height, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
  const VkImageView views[]{worldColor.view, worldDepth.view};
  VkFramebufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  info.renderPass = renderPass;
  info.attachmentCount = 2;
  info.pAttachments = views;
  info.width = alternate_width;
  info.height = alternate_height;
  info.layers = 1;
  check(f.vkCreateFramebuffer(device, &info, nullptr, &worldFramebuffer),
        "world framebuffer");
  image(reconstructedColor.image, reconstructedColor.memory,
        reconstructedColor.view, rw, rh, color_format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
  const VkImageView reconstruction_views[]{reconstructedColor.view,
                                           depthImage.view};
  info.pAttachments = reconstruction_views;
  info.width = rw;
  info.height = rh;
  check(
      f.vkCreateFramebuffer(device, &info, nullptr, &reconstructionFramebuffer),
      "reconstruction framebuffer");
  std::fprintf(stderr,
               "World resolution: internal %ux%u, output %ux%u, guest %ux%u\n",
               alternate_width, alternate_height, rw, rh, width, height);
}
void select_world_target(bool reduced, bool discard) {
  if (!worldColor.image || reduced == reduced_world_active)
    return;
  settle();
  if (!discard) {
    check(f.vkResetCommandPool(device, commandPool, 0),
          "reset world transfer pool");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(f.vkBeginCommandBuffer(cmd, &begin), "begin world transfer");
    const auto source_color_layout =
        in_attachment_layout ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                             : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const auto source_depth_layout =
        in_attachment_layout ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                             : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier(colorImage.image, source_color_layout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            in_attachment_layout ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    barrier(depthImage.image, source_depth_layout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            in_attachment_layout ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                 : VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    for (auto [target, aspect] :
         {std::pair{worldColor.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT)},
          std::pair{worldDepth.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT)}})
      barrier(target, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, aspect);
    blit_img(colorImage.image, rw, rh, worldColor.image, alternate_width,
             alternate_height, VK_IMAGE_ASPECT_COLOR_BIT, VK_FILTER_LINEAR);
    blit_img(depthImage.image, rw, rh, worldDepth.image, alternate_width,
             alternate_height, VK_IMAGE_ASPECT_DEPTH_BIT, VK_FILTER_NEAREST);
    for (auto [target, aspect] :
         {std::pair{worldColor.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT)},
          std::pair{worldDepth.image,
                    VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT)}})
      barrier(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              aspect);
    check(f.vkEndCommandBuffer(cmd), "end world transfer");
    check(f.vkResetFences(device, 1, &fence), "reset world transfer fence");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(f.vkQueueSubmit(queue, 1, &submit, fence), "submit world transfer");
    scene_pending = true;
  }
  std::swap(colorImage, worldColor);
  std::swap(depthImage, worldDepth);
  std::swap(framebuffer, worldFramebuffer);
  std::swap(rw, alternate_width);
  std::swap(rh, alternate_height);
  scale_x = float(rw) / float(width);
  in_attachment_layout = false;
  reduced_world_active = reduced;
  if (!reduced)
    ++world_promotions;
}
