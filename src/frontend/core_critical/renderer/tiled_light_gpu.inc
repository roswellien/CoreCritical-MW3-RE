// Included inside Renderer. All resources share its scene fence and device owner.
VkDescriptorSetLayout tiledSetLayout{};
VkPipelineLayout tiledLayout{};
VkPipeline tiledPipeline{};
VkDescriptorPool tiledPool{};
Buffer tiledViews, tiledLists;
VkDeviceSize tiledViewStride{}, tiledListAlignment{}, tiledRange{};
std::vector<VkDescriptorSet> tiledSets;
std::vector<uint32_t> tiledOffsets;
std::vector<VkDeviceSize> tiledLengths;

void init_tiled() {
  if (tiledPipeline)
    return;
  unsigned count = 0;
  f.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  f.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
  VkPhysicalDeviceProperties p{};
  f.vkGetPhysicalDeviceProperties(gpu, &p);
  if (!(families.at(family).queueFlags & VK_QUEUE_COMPUTE_BIT) ||
      p.limits.maxComputeWorkGroupInvocations < 64 || p.limits.maxComputeWorkGroupSize[0] < 64 ||
      p.limits.maxComputeSharedMemorySize < 256 || p.limits.maxComputeWorkGroupCount[0] < 320 ||
      p.limits.maxComputeWorkGroupCount[1] < 135 || p.limits.maxStorageBufferRange < 11232000)
    throw std::runtime_error("Tiled light compute device limits unavailable");
  const auto alignment = p.limits.minUniformBufferOffsetAlignment;
  tiledViewStride = (sizeof(mw34::tiled::View) + alignment - 1) / alignment * alignment;
  tiledListAlignment = p.limits.minStorageBufferOffsetAlignment;
  tiledRange = std::min<VkDeviceSize>(p.limits.maxStorageBufferRange, 128ULL * 1024 * 1024);
  try {
    const VkDescriptorSetLayoutBinding bindings[3]{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = 3;
    sl.pBindings = bindings;
    check(f.vkCreateDescriptorSetLayout(device, &sl, nullptr, &tiledSetLayout), "tiled set layout");
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &tiledSetLayout;
    check(f.vkCreatePipelineLayout(device, &pl, nullptr, &tiledLayout), "tiled pipeline layout");
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = sizeof(light_cull_tiled_comp_spv);
    sm.pCode = light_cull_tiled_comp_spv;
    VkShaderModule module{};
    check(f.vkCreateShaderModule(device, &sm, nullptr, &module), "tiled shader module");
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = module;
    cp.stage.pName = "main";
    cp.layout = tiledLayout;
    const auto result =
        f.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &tiledPipeline);
    f.vkDestroyShaderModule(device, module, nullptr);
    check(result, "tiled compute pipeline");
  } catch (...) {
    destroy_tiled();
    throw;
  }
}
void destroy_tiled() {
  if (tiledPool)
    f.vkDestroyDescriptorPool(device, tiledPool, nullptr);
  if (tiledPipeline)
    f.vkDestroyPipeline(device, tiledPipeline, nullptr);
  if (tiledLayout)
    f.vkDestroyPipelineLayout(device, tiledLayout, nullptr);
  if (tiledSetLayout)
    f.vkDestroyDescriptorSetLayout(device, tiledSetLayout, nullptr);
  tiledPool = {};
  tiledPipeline = {};
  tiledLayout = {};
  tiledSetLayout = {};
  for (auto *b : {&tiledViews, &tiledLists}) {
    if (b->buffer)
      f.vkDestroyBuffer(device, b->buffer, nullptr);
    if (b->memory)
      f.vkFreeMemory(device, b->memory, nullptr);
    *b = {};
  }
}

void prepare_tiled(const std::vector<mw34::tiled::View> &views) {
  if (views.size() > 64)
    throw std::runtime_error("Too many tiled camera views");
  tiledOffsets.clear();
  tiledLengths.clear();
  tiledSets.clear();
  VkDeviceSize total = 260;
  if (!views.empty()) {
    init_tiled();
    total = 0;
    for (const auto &v : views) {
      const auto checked = mw34::tiled::make_view(v.camera, v.projection, v.depth[0], v.depth[1],
                                                  v.extent[0], v.extent[1]);
      if (checked.extent != v.extent)
        throw std::runtime_error("Tiled view extent mismatch");
      total = (total + tiledListAlignment - 1) / tiledListAlignment * tiledListAlignment;
      const auto length = mw34::tiled::bytes(v);
      if (length > tiledRange || total > tiledRange - length)
        throw std::runtime_error("Tiled view storage exceeds bounded capacity");
      tiledOffsets.push_back(uint32_t(total / 4));
      tiledLengths.push_back(length);
      total += length;
    }
  }
  ensure(tiledLists, total,
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
         false);
  if (views.empty())
    return;
  ensure(tiledViews, tiledViewStride * views.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false);
  for (size_t i = 0; i < views.size(); ++i)
    std::memcpy(static_cast<char *>(tiledViews.mapped) + tiledViewStride * i, &views[i],
                sizeof(views[i]));
  if (tiledPool)
    f.vkDestroyDescriptorPool(device, tiledPool, nullptr);
  tiledPool = {};
  const unsigned n = unsigned(views.size());
  const VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, n * 2},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, n}};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = n;
  pi.poolSizeCount = 2;
  pi.pPoolSizes = sizes;
  check(f.vkCreateDescriptorPool(device, &pi, nullptr, &tiledPool), "tiled descriptor pool");
  std::vector<VkDescriptorSetLayout> layouts(n, tiledSetLayout);
  tiledSets.resize(n);
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = tiledPool;
  ai.descriptorSetCount = n;
  ai.pSetLayouts = layouts.data();
  check(f.vkAllocateDescriptorSets(device, &ai, tiledSets.data()), "tiled descriptor sets");
  for (unsigned i = 0; i < n; ++i) {
    const VkDescriptorBufferInfo info[3]{
        {effectBuffer.buffer, 0, sizeof(mw34::effects::Uniform)},
        {tiledViews.buffer, tiledViewStride * i, sizeof(mw34::tiled::View)},
        {tiledLists.buffer, VkDeviceSize(tiledOffsets[i]) * 4, tiledLengths[i]}};
    VkWriteDescriptorSet writes[3]{};
    for (unsigned b = 0; b < 3; ++b) {
      writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[b].dstSet = tiledSets[i];
      writes[b].dstBinding = b;
      writes[b].descriptorCount = 1;
      writes[b].descriptorType =
          b == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[b].pBufferInfo = &info[b];
    }
    f.vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
  }
}
void record_tiled(const std::vector<mw34::tiled::View> &views) {
  if (views.empty())
    return;
  f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tiledPipeline);
  for (size_t i = 0; i < views.size(); ++i) {
    f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tiledLayout, 0, 1, &tiledSets[i],
                              0, nullptr);
    f.vkCmdDispatch(cmd, views[i].extent[2], views[i].extent[3], 1);
  }
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = tiledLists.buffer;
  barrier.size = VK_WHOLE_SIZE;
  f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
}
