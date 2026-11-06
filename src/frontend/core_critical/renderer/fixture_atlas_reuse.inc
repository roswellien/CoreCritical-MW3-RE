// Renderer-private exact atlas identity. This caches only previously submitted
// depth contents within the same original world frame. All caster preparation,
// source validation, pruning and texture/model updates still execute first.
std::vector<unsigned char> fixtureAtlasIdentity, fixtureAtlasNext;
std::uint64_t fixtureAtlasFrame{}, fixtureAtlasHits{}, fixtureAtlasBuilds{},
    fixtureAtlasMutation{}, fixtureAtlasAudits{};
std::uint64_t fixtureAtlasSubmittedFrame{}, fixtureAtlasSubmittedMutation{};
bool fixtureAtlasValid{}, fixtureAtlasRecorded{};

bool fixture_atlas_reuse_enabled() const {
  return bridge_config.fixture_atlas_reuse;
}
bool fixture_atlas_tracked() const {
  return bridge_config.fixture_atlas_tracked;
}
bool fixture_atlas_audit() const { return bridge_config.fixture_atlas_audit; }
void fixture_atlas_touch() {
  if (++fixtureAtlasMutation == 0)
    throw std::runtime_error("Fixture caster revision overflow");
}
// These are the captured shader inputs before GPU range assignment. The
// original frame and validity callback are still refreshed by every assignment.
// Model/texture backing changes are independently covered by upload counters.
bool fixture_atlas_same_caster(const FixtureCaster &a, const FixtureCaster &b) {
  return a.geometry_key == b.geometry_key && a.count == b.count &&
         a.texture == b.texture && a.sampler == b.sampler &&
         !std::memcmp(a.world.data(), b.world.data(), sizeof(a.world)) &&
         !std::memcmp(a.alpha.data(), b.alpha.data(), sizeof(a.alpha)) &&
         !std::memcmp(a.bounds.low.data(), b.bounds.low.data(),
                      sizeof(a.bounds.low)) &&
         !std::memcmp(a.bounds.high.data(), b.bounds.high.data(),
                      sizeof(a.bounds.high)) &&
         a.tl.size() == b.tl.size() &&
         (a.tl.empty() || !std::memcmp(a.tl.data(), b.tl.data(),
                                       a.tl.size() * sizeof(ModelVertex)));
}
bool fixture_atlas_capture_changed(bool inserted, const FixtureCaster &old,
                                   const FixtureCaster &next) {
  // No comparison is necessary while the existing atlas is already invalid
  // for this frame/revision. All normal assignments and callbacks still run.
  if (!fixture_atlas_tracked() || !fixtureAtlasValid ||
      fixtureAtlasFrame != fixtureAtlasSubmittedFrame ||
      fixtureAtlasMutation != fixtureAtlasSubmittedMutation)
    return false;
  return inserted || !fixture_atlas_same_caster(old, next);
}
// One dependency list serves both comparison and capture. A successful hit
// reads the previous immutable identity without building another byte vector.
template <class Visit>
bool fixture_atlas_visit(const mw34::effects::Uniform &lights, Visit visit,
                         bool metadata_only = false) {
  auto field = [&](const auto &value) {
    static_assert(
        std::is_trivially_copyable_v<std::remove_cvref_t<decltype(value)>>);
    return visit(&value, sizeof(value));
  };
#define MW34_ATLAS_FIELD(value)                                                \
  if (!field(value))                                                           \
  return false
  MW34_ATLAS_FIELD(fixtureAtlasFrame);
  MW34_ATLAS_FIELD(fixtureGeneration);
  MW34_ATLAS_FIELD(fixtureContext);
  MW34_ATLAS_FIELD(fixtureTile);
  MW34_ATLAS_FIELD(fixtureAtlasWidth);
  MW34_ATLAS_FIELD(fixtureAtlasHeight);
  MW34_ATLAS_FIELD(fixtureShadowImage);
  MW34_ATLAS_FIELD(fixtureShadowPipeline);
  if (fixture_atlas_tracked()) {
    MW34_ATLAS_FIELD(fixtureAtlasMutation);
    MW34_ATLAS_FIELD(uploads);
    MW34_ATLAS_FIELD(model_uploads);
    MW34_ATLAS_FIELD(mip_levels_uploaded);
    MW34_ATLAS_FIELD(fixtureTlBuffer.buffer);
    MW34_ATLAS_FIELD(std::uint64_t(model_buffers.size()));
  }
  // The depth pass reads the active tile index, shadow parameters and light
  // start/radius. Emission colour/endpoints do not enter either shadow shader.
  const unsigned count =
      std::min(unsigned(lights.params[0]), mw34::effects::capacity);
  MW34_ATLAS_FIELD(count);
  for (unsigned i = 0; i < count; ++i) {
    MW34_ATLAS_FIELD(i);
    MW34_ATLAS_FIELD(lights.shadow[i]);
    if (lights.shadow[i][0] >= .5f) {
      MW34_ATLAS_FIELD(lights.lights[i].start);
    }
  }
  MW34_ATLAS_FIELD(std::uint64_t(fixtureFrameCasters.size()));
  if (metadata_only)
    return true;
  for (const auto *caster : fixtureFrameCasters) {
    MW34_ATLAS_FIELD(caster->buffer);
    MW34_ATLAS_FIELD(caster->first);
    MW34_ATLAS_FIELD(caster->count);
    MW34_ATLAS_FIELD(caster->world);
    MW34_ATLAS_FIELD(caster->alpha);
    MW34_ATLAS_FIELD(caster->bounds.low);
    MW34_ATLAS_FIELD(caster->bounds.high);
    // Descriptor handles are recreated per scene. Their effective texture
    // contents/view and sampler identity, rather than allocation, matter.
    MW34_ATLAS_FIELD(caster->texture);
    MW34_ATLAS_FIELD(caster->sampler);
    MW34_ATLAS_FIELD(caster->texture->image);
    MW34_ATLAS_FIELD(caster->texture->view);
    MW34_ATLAS_FIELD(caster->texture->hash);
    MW34_ATLAS_FIELD(caster->texture->valid);
    MW34_ATLAS_FIELD(caster->texture->width);
    MW34_ATLAS_FIELD(caster->texture->height);
    MW34_ATLAS_FIELD(caster->texture->levels);
    MW34_ATLAS_FIELD(caster->geometry_key);
    if (caster->geometry_key) {
      const auto &geometry = model_buffers.at(caster->geometry_key);
      MW34_ATLAS_FIELD(geometry.hash);
      MW34_ATLAS_FIELD(geometry.count);
      MW34_ATLAS_FIELD(geometry.uploaded);
    }
    MW34_ATLAS_FIELD(std::uint64_t(caster->tl.size()));
    if (!caster->tl.empty() &&
        !visit(caster->tl.data(), caster->tl.size() * sizeof(ModelVertex)))
      return false;
  }
#undef MW34_ATLAS_FIELD
  return true;
}
bool fixture_atlas_unchanged(const mw34::effects::Uniform &lights) {
  if (!fixture_atlas_reuse_enabled())
    return false;
  const bool tracked = fixture_atlas_tracked(), audit = fixture_atlas_audit();
  const bool compact = tracked && !audit;
  if (fixtureAtlasValid) {
    size_t offset = 0;
    const bool same = fixture_atlas_visit(
        lights,
        [&](const void *data, size_t bytes) {
          if (bytes > fixtureAtlasIdentity.size() - offset ||
              std::memcmp(fixtureAtlasIdentity.data() + offset, data, bytes))
            return false;
          offset += bytes;
          return true;
        },
        tracked);
    if (same && (tracked || offset == fixtureAtlasIdentity.size())) {
      if (tracked && audit) {
        offset = 0;
        const bool exact = fixture_atlas_visit(lights, [&](const void *data,
                                                           size_t bytes) {
          if (bytes > fixtureAtlasIdentity.size() - offset ||
              std::memcmp(fixtureAtlasIdentity.data() + offset, data, bytes))
            return false;
          offset += bytes;
          return true;
        });
        if (!exact || offset != fixtureAtlasIdentity.size())
          throw std::runtime_error(
              "Fixture atlas revision missed a changed shader input");
        ++fixtureAtlasAudits;
      }
      ++fixtureAtlasHits;
      return true;
    }
  }
  fixtureAtlasNext.clear();
  fixtureAtlasNext.reserve(fixtureAtlasIdentity.size());
  fixture_atlas_visit(
      lights,
      [&](const void *data, size_t size) {
        const auto *bytes = static_cast<const unsigned char *>(data);
        fixtureAtlasNext.insert(fixtureAtlasNext.end(), bytes, bytes + size);
        return true;
      },
      compact);
  return false;
}
void fixture_atlas_submitted() {
  // Recording alone cannot establish atlas contents: a later exception could
  // abandon that command buffer. Commit only after successful queue submit.
  if (!fixtureAtlasRecorded)
    return;
  fixtureAtlasIdentity.swap(fixtureAtlasNext);
  fixtureAtlasValid = true;
  fixtureAtlasSubmittedFrame = fixtureAtlasFrame;
  fixtureAtlasSubmittedMutation = fixtureAtlasMutation;
  fixtureAtlasRecorded = false;
  ++fixtureAtlasBuilds;
}
