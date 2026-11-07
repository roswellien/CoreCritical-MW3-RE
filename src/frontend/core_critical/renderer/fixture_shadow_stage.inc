// Local fixture shadows use an atlas of six perspective views per light.
// Original source proofs retain static world casters across view culling;
// moving geometry is refreshed per frame, including split scene batches.
struct FixtureCaster {
  VkBuffer buffer{};
  unsigned first{}, count{};
  std::uint64_t geometry_key{};
  mw34::fixture_shadow::Matrix world = mw34::fixture_shadow::identity;
  mw34::fixture_shadow::Bounds bounds;
  std::vector<ModelVertex> tl;
  std::function<bool()> valid;
  std::uint64_t frame{};
  Tex *texture{};
  unsigned sampler{};
  std::array<float, 4> alpha{};
  VkDescriptorSet set{};
};
std::map<std::pair<std::uint64_t, std::uint64_t>, FixtureCaster> fixtureCasters;
std::vector<FixtureCaster *> fixtureFrameCasters;
std::uint64_t fixtureGeneration{~0ULL}, fixtureContext{~0ULL};
Buffer fixtureTlBuffer;
VkImage fixtureShadowImage{};
VkDeviceMemory fixtureShadowMemory{};
VkImageView fixtureShadowView{};
VkSampler fixtureShadowSampler{};
VkRenderPass fixtureShadowPass{};
VkFramebuffer fixtureShadowFramebuffer{};
VkPipelineLayout fixtureShadowLayout{};
VkPipeline fixtureShadowPipeline{};
VkShaderModule fixtureShadowVert{}, fixtureShadowFrag{};
unsigned fixtureTile{}, fixtureAtlasWidth{1}, fixtureAtlasHeight{1};

#include "fixture_atlas_reuse.inc"
void init_fixture_shadow() {
  const auto &settings = effects_settings;
  fixtureTile = settings.fixture_shadows ? settings.fixture_shadow_size : 0;
  if (fixtureTile) {
    fixtureAtlasWidth = fixtureTile * mw34::fixture_shadow::columns;
    fixtureAtlasHeight = fixtureTile * mw34::fixture_shadow::rows;
  }
  VkFormat format = VK_FORMAT_D32_SFLOAT;
  VkFormatProperties properties{};
  const VkFormatFeatureFlags need =
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  f.vkGetPhysicalDeviceFormatProperties(gpu, format, &properties);
  if ((properties.optimalTilingFeatures & need) != need) {
    format = VK_FORMAT_D16_UNORM;
    f.vkGetPhysicalDeviceFormatProperties(gpu, format, &properties);
    if ((properties.optimalTilingFeatures & need) != need)
      throw std::runtime_error(
          "Fixture shadow depth attachment/sampling unavailable");
  }
  VkPhysicalDeviceProperties deviceProperties{};
  f.vkGetPhysicalDeviceProperties(gpu, &deviceProperties);
  const auto &limits = deviceProperties.limits;
  if (fixtureAtlasWidth > limits.maxImageDimension2D ||
      fixtureAtlasHeight > limits.maxImageDimension2D ||
      fixtureAtlasWidth > limits.maxFramebufferWidth ||
      fixtureAtlasHeight > limits.maxFramebufferHeight)
    throw std::runtime_error(
        "Fixture shadow atlas exceeds device image/framebuffer limits");
  image(fixtureShadowImage, fixtureShadowMemory, fixtureShadowView,
        fixtureAtlasWidth, fixtureAtlasHeight, format,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = samplerInfo.addressModeV =
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  check(f.vkCreateSampler(device, &samplerInfo, nullptr, &fixtureShadowSampler),
        "fixture shadow sampler");
  if (!fixtureTile)
    return;
  VkAttachmentDescription attachment{};
  attachment.format = format;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference depth{0,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.pDepthStencilAttachment = &depth;
  std::array<VkSubpassDependency, 2> dependencies{
      {{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        0},
       {0, VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        0}}};
  VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  pass.attachmentCount = 1;
  pass.pAttachments = &attachment;
  pass.subpassCount = 1;
  pass.pSubpasses = &subpass;
  pass.dependencyCount = 2;
  pass.pDependencies = dependencies.data();
  check(f.vkCreateRenderPass(device, &pass, nullptr, &fixtureShadowPass),
        "fixture shadow render pass");
  VkFramebufferCreateInfo framebufferInfo{
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebufferInfo.renderPass = fixtureShadowPass;
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.pAttachments = &fixtureShadowView;
  framebufferInfo.width = fixtureAtlasWidth;
  framebufferInfo.height = fixtureAtlasHeight;
  framebufferInfo.layers = 1;
  check(f.vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                              &fixtureShadowFramebuffer),
        "fixture shadow framebuffer");
  VkPushConstantRange push{
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
  VkPipelineLayoutCreateInfo layout{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 1;
  layout.pSetLayouts = &setLayout;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push;
  check(
      f.vkCreatePipelineLayout(device, &layout, nullptr, &fixtureShadowLayout),
      "fixture shadow pipeline layout");
  auto module = [&](const std::uint32_t *code, size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule value{};
    check(f.vkCreateShaderModule(device, &info, nullptr, &value),
          "fixture shadow shader");
    return value;
  };
  fixtureShadowVert =
      module(fixture_shadow_vert_spv, sizeof(fixture_shadow_vert_spv));
  fixtureShadowFrag =
      module(fixture_shadow_frag_spv, sizeof(fixture_shadow_frag_spv));
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  for (auto &stage : stages) {
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.pName = "main";
  }
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = fixtureShadowVert;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fixtureShadowFrag;
  VkVertexInputBindingDescription binding{0, sizeof(ModelVertex),
                                          VK_VERTEX_INPUT_RATE_VERTEX};
  std::array<VkVertexInputAttributeDescription, 2> attributes{
      {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
       {1, 0, VK_FORMAT_R32G32_SFLOAT, 24}}};
  VkPipelineVertexInputStateCreateInfo vertex{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex.vertexBindingDescriptionCount = 1;
  vertex.pVertexBindingDescriptions = &binding;
  vertex.vertexAttributeDescriptionCount = 2;
  vertex.pVertexAttributeDescriptions = attributes.data();
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.lineWidth = 1;
  raster.depthBiasEnable = VK_TRUE;
  raster.depthBiasSlopeFactor = 1.5f;
  VkPipelineMultisampleStateCreateInfo samples{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depthState{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depthState.depthTestEnable = depthState.depthWriteEnable = VK_TRUE;
  depthState.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamicStates.data();
  VkGraphicsPipelineCreateInfo pipeline{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline.stageCount = 2;
  pipeline.pStages = stages.data();
  pipeline.pVertexInputState = &vertex;
  pipeline.pInputAssemblyState = &assembly;
  pipeline.pViewportState = &viewport;
  pipeline.pRasterizationState = &raster;
  pipeline.pMultisampleState = &samples;
  pipeline.pDepthStencilState = &depthState;
  pipeline.pColorBlendState = &blend;
  pipeline.pDynamicState = &dynamic;
  pipeline.layout = fixtureShadowLayout;
  pipeline.renderPass = fixtureShadowPass;
  check(f.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline,
                                    nullptr, &fixtureShadowPipeline),
        "fixture shadow pipeline");
  std::fprintf(stderr,
               "Fixture shadows: %u lights, six %ux%u views each, %ux%u atlas; "
               "original world and model casters, alpha-tested cutouts\n",
               mw34::effects::capacity, fixtureTile, fixtureTile,
               fixtureAtlasWidth, fixtureAtlasHeight);
}

mw34::effects::Uniform
prepare_fixture_shadows(const Scene &scene, const std::vector<Tex *> &bound,
                        const std::vector<unsigned char> &vertices) {
  auto lights = scene.effect_lights;
  for (auto &meta : lights.shadow)
    meta = {};
  fixtureFrameCasters.clear();
  fixtureAtlasFrame = scene.world_frame;
  fixtureAtlasRecorded = false;
  if (!fixtureTile || scene.hud || scene.frontend_canvas)
    return lights;
  if (fixtureGeneration != scene.world_generation ||
      fixtureContext != scene.world_context) {
    fixture_atlas_touch();
    fixtureCasters.clear();
    fixtureGeneration = scene.world_generation;
    fixtureContext = scene.world_context;
  }
  const unsigned count = std::min(unsigned(lights.params[0]),
                                  mw34::effects::capacity),
                 weapons = std::min(unsigned(lights.params[1]), count);
  if (count == weapons)
    return lights;
  mw34::fixture_shadow::Point eye{};
  bool camera = false;
  for (const auto &draw : scene.draws) {
    if (draw.sky)
      continue;
    if (draw.sun_tl) {
      const auto &m = draw.sun_tl->camera_world;
      eye = {m[3], m[7], m[11]};
      camera = true;
      break;
    }
    if (draw.model && draw.model->sun_eligible) {
      const auto &m = draw.model->camera;
      eye = {m[3], m[7], m[11]};
      camera = true;
    }
  }
  // GPU model camera fields are local-to-camera. Use the captured scene
  // camera supplied by the original authenticated world view instead.
  if (scene.world_eye_valid) {
    eye = scene.world_eye;
    camera = true;
  }
  const float retention = effects_settings.fixture_retain_distance;
  for (auto it = fixtureCasters.begin(); it != fixtureCasters.end();) {
    auto &caster = it->second;
    const bool invalid =
        caster.valid ? !caster.valid() : scene.world_frame > caster.frame + 1;
    if (invalid || (camera && !caster.bounds.overlaps(eye, retention * 2))) {
      fixture_atlas_touch();
      it = fixtureCasters.erase(it);
    } else
      ++it;
  }
  struct TlVertex {
    float x, y, z, rhw;
    unsigned rgb, spec;
    float u, v;
  };
  for (const auto &draw : scene.draws) {
    if (!draw.count || draw.sky || draw.key.blend || !draw.key.zenable ||
        !draw.key.zwrite || !sun_lit(draw))
      continue;
    FixtureCaster caster;
    caster.frame = scene.world_frame;
    caster.texture =
        draw.texture >= 0 ? bound.at(size_t(draw.texture)) : &white;
    caster.sampler = draw.sampler;
    caster.alpha = {float(draw.push.mode[3] & 255),
                    float((draw.push.mode[3] >> 8) & 255),
                    float((draw.push.mode[0] & 32) != 0), 0};
    std::pair<std::uint64_t, std::uint64_t> key{};
    if (draw.model) {
      const auto &instance = *draw.model;
      auto geometry = model_buffers.find(instance.geometry->key);
      if (geometry == model_buffers.end())
        continue;
      caster.geometry_key = instance.geometry->key;
      caster.buffer = geometry->second.local.buffer;
      caster.count = geometry->second.count;
      caster.world = instance.sun_world;
      for (const auto &v : instance.geometry->vertices) {
        mw34::fixture_shadow::Point p{};
        for (unsigned r = 0; r < 3; ++r)
          p[r] = caster.world[r * 4] * v.x + caster.world[r * 4 + 1] * v.y +
                 caster.world[r * 4 + 2] * v.z + caster.world[r * 4 + 3];
        caster.bounds.add(p);
      }
      if (instance.world && draw.shadow_source && draw.shadow_valid) {
        key = {draw.shadow_source, 0};
        caster.valid = draw.shadow_valid;
      } else
        key = {instance.shadow_source ? instance.shadow_source
                                      : instance.geometry->key,
               instance.geometry->key};
    } else if (draw.sun_tl) {
      std::uint64_t hash = 0xcbf29ce484222325ULL;
      for (unsigned i = 0; i < draw.count; ++i) {
        TlVertex v;
        const size_t offset = size_t(draw.first + i) * sizeof(TlVertex);
        if (offset + sizeof(v) > vertices.size())
          throw std::runtime_error(
              "Fixture shadow TL source outside captured vertices");
        std::memcpy(&v, vertices.data() + offset, sizeof(v));
        const auto p = mw34::effects::reconstruct(
            v.x, v.y, v.z, draw.sun_tl->projection, draw.sun_tl->camera_world);
        caster.bounds.add(p);
        caster.tl.push_back({p[0], p[1], p[2], 0, 0, 0, v.u, v.v});
        for (float x : p) {
          std::uint32_t bits;
          std::memcpy(&bits, &x, 4);
          hash ^= bits;
          hash *= 0x100000001b3ULL;
        }
      }
      caster.count = unsigned(caster.tl.size());
      key = {0, hash};
    } else
      continue;
    bool relevant = false;
    for (unsigned i = weapons; i < count; ++i)
      relevant |= caster.bounds.overlaps({lights.lights[i].start[0],
                                          lights.lights[i].start[1],
                                          lights.lights[i].start[2]},
                                         lights.lights[i].start[3]);
    if (!relevant)
      continue;
    if (fixtureCasters.size() >= 32768 && !fixtureCasters.contains(key))
      throw std::runtime_error(
          "Retained fixture shadow caster capacity exceeded");
    auto [stored, inserted] = fixtureCasters.try_emplace(key);
    if (fixture_atlas_capture_changed(inserted, stored->second, caster))
      fixture_atlas_touch();
    stored->second = std::move(caster);
  }
  std::vector<ModelVertex> tl;
  for (auto &[key, caster] : fixtureCasters) {
    bool relevant = false;
    for (unsigned i = weapons; i < count; ++i)
      relevant |= caster.bounds.overlaps({lights.lights[i].start[0],
                                          lights.lights[i].start[1],
                                          lights.lights[i].start[2]},
                                         lights.lights[i].start[3]);
    if (!relevant)
      continue;
    if (caster.geometry_key) {
      auto geometry = model_buffers.find(caster.geometry_key);
      if (geometry == model_buffers.end())
        continue;
      caster.buffer = geometry->second.local.buffer;
      caster.count = geometry->second.count;
    }
    if (!caster.tl.empty()) {
      caster.first = unsigned(tl.size());
      tl.insert(tl.end(), caster.tl.begin(), caster.tl.end());
    }
    fixtureFrameCasters.push_back(&caster);
  }
  if (!tl.empty()) {
    ensure(fixtureTlBuffer, tl.size() * sizeof(ModelVertex),
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(fixtureTlBuffer.mapped, tl.data(),
                tl.size() * sizeof(ModelVertex));
    for (auto *caster : fixtureFrameCasters)
      if (!caster->tl.empty())
        caster->buffer = fixtureTlBuffer.buffer;
  }
  if (!fixtureFrameCasters.empty())
    for (unsigned i = weapons; i < count; ++i)
      lights.shadow[i] = {1, .10f, lights.lights[i].start[3], .025f};
  return lights;
}

void fixture_shadow_sets(
    std::map<std::pair<Tex *, unsigned>, VkDescriptorSet> &sets) {
  for (auto *caster : fixtureFrameCasters) {
    Tex *texture = caster->alpha[2] > .5f ? caster->texture : &white;
    const unsigned sampling = caster->alpha[2] > .5f ? caster->sampler : 0x113;
    auto key = std::make_pair(texture, sampling);
    auto found = sets.find(key);
    if (found == sets.end()) {
      VkDescriptorSetAllocateInfo info{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      info.descriptorPool = descriptorPool;
      info.descriptorSetCount = 1;
      info.pSetLayouts = &setLayout;
      VkDescriptorSet set{};
      check(f.vkAllocateDescriptorSets(device, &info, &set),
            "fixture caster texture descriptor");
      VkDescriptorImageInfo imageInfo{sampler(sampling), texture->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = set;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.pImageInfo = &imageInfo;
      f.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
      found = sets.emplace(key, set).first;
    }
    caster->set = found->second;
  }
}

void record_fixture_shadows(const mw34::effects::Uniform &lights) {
  const bool batch = bridge_config.shadow_batch;
  if (!fixtureTile || fixtureFrameCasters.empty())
    return;
  if (fixture_atlas_unchanged(lights))
    return;
  VkClearValue clear{};
  clear.depthStencil.depth = 1;
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = fixtureShadowPass;
  pass.framebuffer = fixtureShadowFramebuffer;
  pass.renderArea = {{0, 0}, {fixtureAtlasWidth, fixtureAtlasHeight}};
  pass.clearValueCount = 1;
  pass.pClearValues = &clear;
  f.vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
  f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      fixtureShadowPipeline);
  const unsigned count =
      std::min(unsigned(lights.params[0]), mw34::effects::capacity);
  for (unsigned i = 0; i < count; ++i) {
    if (lights.shadow[i][0] < .5f)
      continue;
    const auto &light = lights.lights[i];
    for (unsigned side = 0; side < 6; ++side) {
      const unsigned tile = i * 6 + side,
                     x = tile % mw34::fixture_shadow::columns * fixtureTile,
                     y = tile / mw34::fixture_shadow::columns * fixtureTile;
      VkViewport viewport{float(x),           float(y), float(fixtureTile),
                          float(fixtureTile), 0,        1};
      VkRect2D scissor{{int(x), int(y)}, {fixtureTile, fixtureTile}};
      f.vkCmdSetViewport(cmd, 0, 1, &viewport);
      f.vkCmdSetScissor(cmd, 0, 1, &scissor);
      const auto clip = mw34::fixture_shadow::face(
          {light.start[0], light.start[1], light.start[2]}, side,
          lights.shadow[i][1], light.start[3]);
      struct Push {
        mw34::fixture_shadow::Clip clip;
        std::array<float, 4> alpha;
      };
      static_assert(sizeof(Push) == 80);
      VkBuffer batchBuffer{};
      VkDescriptorSet batchSet{};
      Push batchPush{};
      unsigned batchFirst = 0, batchCount = 0;
      auto emit = [&] {
        if (!batchCount)
          return;
        const VkDeviceSize zero = 0;
        f.vkCmdBindVertexBuffers(cmd, 0, 1, &batchBuffer, &zero);
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  fixtureShadowLayout, 0, 1, &batchSet, 0,
                                  nullptr);
        f.vkCmdPushConstants(cmd, fixtureShadowLayout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(batchPush), &batchPush);
        f.vkCmdDraw(cmd, batchCount, 1, batchFirst, 0);
        batchCount = 0;
      };
      for (const auto *caster : fixtureFrameCasters) {
        if (!caster->bounds.overlaps(
                {light.start[0], light.start[1], light.start[2]},
                light.start[3]))
          continue;
        // A conservative cube-face test avoids drawing a nearby
        // caster into the five faces it cannot intersect.
        bool outside = false;
        for (unsigned plane = 0; plane < 6; ++plane) {
          bool all = true;
          for (unsigned corner = 0; corner < 8; ++corner) {
            std::array<float, 4> p{
                (corner & 1) ? caster->bounds.high[0] : caster->bounds.low[0],
                (corner & 2) ? caster->bounds.high[1] : caster->bounds.low[1],
                (corner & 4) ? caster->bounds.high[2] : caster->bounds.low[2],
                1};
            std::array<float, 4> q{};
            for (unsigned r = 0; r < 4; ++r)
              for (unsigned c = 0; c < 4; ++c)
                q[r] += clip[r * 4 + c] * p[c];
            const float d = plane == 0   ? q[0] + q[3]
                            : plane == 1 ? q[3] - q[0]
                            : plane == 2 ? q[1] + q[3]
                            : plane == 3 ? q[3] - q[1]
                            : plane == 4 ? q[2]
                                         : q[3] - q[2];
            all &= d < 0;
          }
          if (all) {
            outside = true;
            break;
          }
        }
        if (outside)
          continue;
        const Push push{mw34::fixture_shadow::compose(clip, caster->world),
                        caster->alpha};
        if (batch && caster->count) {
          const std::uint64_t total = std::uint64_t(batchCount) + caster->count;
          // Incomplete triangle tails cannot cross a draw boundary.
          const bool same =
              batchBuffer == caster->buffer && batchSet == caster->set &&
              !std::memcmp(&batchPush, &push, sizeof(push)) &&
              std::uint64_t(batchFirst) + batchCount == caster->first &&
              !(batchCount % 3) && !(caster->count % 3) &&
              total <= 0xffffffffULL;
          if (batchCount && !same)
            emit();
          if (!batchCount) {
            batchBuffer = caster->buffer;
            batchSet = caster->set;
            batchPush = push;
            batchFirst = caster->first;
          }
          batchCount += caster->count;
          continue;
        }
        if (batch)
          emit();
        const VkDeviceSize zero = 0;
        f.vkCmdBindVertexBuffers(cmd, 0, 1, &caster->buffer, &zero);
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  fixtureShadowLayout, 0, 1, &caster->set, 0,
                                  nullptr);
        f.vkCmdPushConstants(cmd, fixtureShadowLayout,
                             VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(push), &push);
        f.vkCmdDraw(cmd, caster->count, 1, caster->first, 0);
      }
      if (batch)
        emit();
    }
  }
  f.vkCmdEndRenderPass(cmd);
  if (fixture_atlas_reuse_enabled())
    fixtureAtlasRecorded = true;
}

void drop_fixture_shadow() {
  if (fixture_atlas_reuse_enabled())
    std::fprintf(stderr,
                 "Fixture atlas reuse: %llu unchanged passes reused, %llu "
                 "changed passes submitted, %llu tracked hits fully audited\n",
                 (unsigned long long)fixtureAtlasHits,
                 (unsigned long long)fixtureAtlasBuilds,
                 (unsigned long long)fixtureAtlasAudits);
  fixtureAtlasValid = false;
  fixtureAtlasRecorded = false;
  fixtureAtlasIdentity.clear();
  fixtureAtlasNext.clear();
  fixtureCasters.clear();
  fixtureFrameCasters.clear();
  if (fixtureShadowPipeline)
    f.vkDestroyPipeline(device, fixtureShadowPipeline, nullptr);
  if (fixtureShadowLayout)
    f.vkDestroyPipelineLayout(device, fixtureShadowLayout, nullptr);
  if (fixtureShadowFramebuffer)
    f.vkDestroyFramebuffer(device, fixtureShadowFramebuffer, nullptr);
  if (fixtureShadowPass)
    f.vkDestroyRenderPass(device, fixtureShadowPass, nullptr);
  if (fixtureShadowSampler)
    f.vkDestroySampler(device, fixtureShadowSampler, nullptr);
  if (fixtureShadowView)
    f.vkDestroyImageView(device, fixtureShadowView, nullptr);
  if (fixtureShadowImage)
    f.vkDestroyImage(device, fixtureShadowImage, nullptr);
  if (fixtureShadowMemory)
    f.vkFreeMemory(device, fixtureShadowMemory, nullptr);
  if (fixtureShadowVert)
    f.vkDestroyShaderModule(device, fixtureShadowVert, nullptr);
  if (fixtureShadowFrag)
    f.vkDestroyShaderModule(device, fixtureShadowFrag, nullptr);
  if (fixtureTlBuffer.buffer)
    f.vkDestroyBuffer(device, fixtureTlBuffer.buffer, nullptr);
  if (fixtureTlBuffer.memory)
    f.vkFreeMemory(device, fixtureTlBuffer.memory, nullptr);
}
