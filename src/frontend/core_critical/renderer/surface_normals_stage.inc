// surface_normals_stage.inc
// Full-content material matching for original textured geometry on any map.

struct SurfaceNormalState {
  bool loaded{false};
  std::uint64_t peak_decoded_bytes{};
  mw34::materials::WorldNormalCache cache{};
  std::string native_material_bytes;
  std::uint64_t expected_mesh_uv_hash{};
  std::uint32_t expected_polygon_count{};
  std::shared_ptr<mw34::materials::SurfaceMaterialPack> native_material_pack;
  std::set<std::pair<std::uint64_t, std::uint64_t>> traced_native;
  std::set<std::tuple<std::uintptr_t, std::uintptr_t, std::uint64_t>>
      traced_sources;
} surface_normals_;

struct NativeBindingContext {
  const zcls::Node *node{};
  const zmod::Model *model{};
  ::platform::MaterialDrawContext material{};
  std::uint64_t epoch{};
  bool material_active{};
  bool fingerprint_computed{};
  std::optional<std::uint64_t> fingerprint;
} native_context_;
std::uint64_t binding_rejected_mismatch_{};
std::uint64_t native_fingerprint_evaluations_{};
struct NativeDeferredBinding final : ::platform::DeferredMaterialBinding {
  std::weak_ptr<const int> owner;
  mw34::materials::SurfaceKey key{};
  std::uint64_t epoch{}, version{};
  int texture{-1}, normal_index{-1};
  std::array<float, 4> surface_parameters{};
};
const std::shared_ptr<const int> material_binding_owner_ =
    std::make_shared<const int>(0);
std::shared_ptr<const ::platform::DeferredMaterialBinding>
    deferred_material_binding_;

bool reject_surface_binding(const char *reason) {
  ++binding_rejected_mismatch_;
  if (binding_rejected_mismatch_ <= 8)
    std::fprintf(stderr, "[SURFACE_BINDING_REJECT] epoch=%llu reason=%s\n",
                 (unsigned long long)group_load_epoch_, reason);
  return false;
}

#include "world_normals_pages.inc"

bool surface_normals_enabled() const { return lighting_.normals; }

void surface_normals_load() {
  if (surface_normals_.loaded) {
    return;
  }
  // Transactional build: fill a local cache first and publish only on success,
  // so an exception leaves no partial entries behind for a subsequent attempt.
  mw34::materials::WorldNormalCache cache{};
  std::vector<std::string> authored_paths;
  if (!bridge_config_.surface_normal_catalog.empty())
    authored_paths.push_back(bridge_config_.surface_normal_catalog);
  authored_paths.insert(authored_paths.end(),
                        bridge_config_.environment_authored_manifests.begin(),
                        bridge_config_.environment_authored_manifests.end());
  std::uint64_t authored_total{};
  for (const auto &authored_path : authored_paths) {
    const auto prior_owners = cache.authored;
    for (const auto &cat_path : mw34::materials::surface_normal_catalog_paths(
             std::filesystem::u8path(authored_path))) {
      const auto bytes = std::filesystem::file_size(cat_path);
      if (bytes > mw34::materials::authored_normal_budget - authored_total)
        throw std::runtime_error(
            "Combined authored normal catalogs exceed byte budget");
      authored_total += bytes;
      std::ifstream file(cat_path, std::ios::binary);
      if (!file) {
        throw std::runtime_error("Configured surface normal page is missing");
      }
      auto parsed = mw34::materials::read_surface_normals(file);
      const std::size_t base_idx = cache.textures.size();
      for (const auto &[key, idx] : parsed.bindings) {
        // Explicit catalog first, then deterministic region ownership order.
        if (prior_owners.count(key))
          continue;
        if (idx == mw34::materials::no_normal) {
          // Explicit no_normal precedence over any world binding.
          cache.register_authored(key, -1);
        } else {
          const std::uint64_t absolute = static_cast<std::uint64_t>(base_idx) +
                                         static_cast<std::uint64_t>(idx);
          if (absolute >
              std::uint64_t(
                  std::numeric_limits<int>::max())) { // overflow check before
                                                      // int conversion
            throw std::runtime_error("Surface normal texture index overflow");
          }
          cache.register_authored(key, static_cast<int>(absolute));
        }
      }
      for (auto &img : parsed.pack.images) {
        mw34::materials::WorldNormalCache::TextureRecord rec{};
        rec.name = std::move(img.name);
        rec.width = img.width;
        rec.height = img.height;
        rec.hash = img.hash;
        rec.pixels32 = std::move(img.pixels);
        cache.textures.push_back(std::move(rec));
      }
    }
  }
  surface_normals_load_world(cache);
  std::string native_bytes;
  std::uint64_t expected_hash{};
  std::uint32_t expected_polygons{};
  if (!bridge_config_.native_material_sidecar.empty()) {
    if (geometry_mode_ != cc::geometry::Mode::legacy)
      throw std::runtime_error(
          "Native UV-qualified materials require the legacy geometry route");
    if (!mw34::materials::valid_surface_identifier(
            bridge_config_.native_material_asset_id,
            mw34::materials::max_surface_identifier_bytes, true))
      throw std::runtime_error(
          "Native material sidecar requires an explicit valid asset ID");
    const auto &path = bridge_config_.native_material_sidecar;
    const auto size = std::filesystem::file_size(path);
    if (size < 32 || size > mw34::materials::max_surface_sidecar_bytes)
      throw std::runtime_error("Native material sidecar size is invalid");
    std::ifstream input(path, std::ios::binary);
    native_bytes.resize(std::size_t(size));
    if (!input.read(native_bytes.data(), std::streamsize(size)) ||
        input.peek() != std::char_traits<char>::eof())
      throw std::runtime_error(
          "Native material sidecar changed during admission");
    std::uint32_t version{};
    std::memcpy(&version, native_bytes.data() + 8, 4);
    std::memcpy(&expected_polygons, native_bytes.data() + 12, 4);
    std::memcpy(&expected_hash, native_bytes.data() + 24, 8);
    if (std::memcmp(native_bytes.data(), "BWSFM1\0\0", 8) || version != 1 ||
        !expected_polygons || expected_polygons > 4096)
      throw std::runtime_error("Native material sidecar header is invalid");
  }
  surface_normals_.native_material_bytes = std::move(native_bytes);
  surface_normals_.expected_mesh_uv_hash = expected_hash;
  surface_normals_.expected_polygon_count = expected_polygons;
  surface_normals_.cache = std::move(cache);
  surface_normals_.loaded = true;
}

// BWSNR catalogs authenticate texel identity. BWSFM1 separately supplies the
// authored mesh/UV compatibility expectation; a first observed UV is not used
// as a substitute for that expectation.
bool native_surface_binding_matches(const mw34::materials::SurfaceKey &key,
                                    int texture_handle) {
  const bool strict = !bridge_config_.native_material_sidecar.empty();
  if (!strict && !bridge_config_.trace_native_bindings)
    return true;
  VisualFloatScope preserve;
  const auto &context = native_context_;
  if (!context.node || !context.model || !context.material_active ||
      context.epoch != group_load_epoch_ ||
      context.node->model != context.model ||
      context.material.model != context.model ||
      context.material.polygon >= context.model->count(0x10) ||
      !context.model->polygons)
    return strict
               ? reject_surface_binding("missing/stale native producer scope")
               : true;
  const auto &polygon = context.model->polygons[context.material.polygon];
  const auto *material = polygon.material;
  if (!material || context.material.material != material ||
      !material->texture ||
      context.material.texture_entry != material->texture ||
      !material->texture->surface)
    return strict ? reject_surface_binding(
                        "material/texture object identity mismatch")
                  : true;
  const auto &source_texture =
      *static_cast<const zvid::D3DTexture *>(material->texture->surface);
  if (source_texture.handle != texture_handle || texture_handle < 0 ||
      std::size_t(texture_handle) >= textures_.size())
    return strict ? reject_surface_binding(
                        "texture handle no longer belongs to material")
                  : true;
  const auto &source = textures_[std::size_t(texture_handle)];
  if (source.group_load_epoch != context.epoch || source.surface_key < key ||
      key < source.surface_key)
    return strict ? reject_surface_binding("surface content/epoch mismatch")
                  : true;
  // Inventory tracing is bounded and never supplies an expected compatibility
  // hash to the guard. Strict qualification always recomputes live source data.
  if (!strict && bridge_config_.trace_native_bindings) {
    if (surface_normals_.traced_sources.size() >= 512 ||
        !surface_normals_.traced_sources
             .emplace(reinterpret_cast<std::uintptr_t>(context.model),
                      reinterpret_cast<std::uintptr_t>(material), key.hash)
             .second)
      return true;
  }
  if (!native_context_.fingerprint_computed) {
    native_context_.fingerprint = native_mesh_uv_hash(*context.model);
    native_context_.fingerprint_computed = true;
    ++native_fingerprint_evaluations_;
  }
  const auto fingerprint = native_context_.fingerprint;
  if (!fingerprint)
    return strict ? reject_surface_binding(
                        "unsupported native source mesh/UV stream")
                  : true;
  if (bridge_config_.trace_native_bindings &&
      surface_normals_.traced_native.size() < 512 &&
      surface_normals_.traced_native.emplace(*fingerprint, key.hash).second)
    std::fprintf(
        stderr,
        "[NATIVE_SURFACE_IDENTITY] node=%.32s texture=%.20s polygon=%u "
        "polygons=%u mesh_uv_hash=0x%llx "
        "surface={%u,%u,%u,0x%llx} epoch=%llu uv_metadata=%s\n",
        context.node->name(), material->texture->name, context.material.polygon,
        context.model->count(0x10), (unsigned long long)*fingerprint, key.width,
        key.height, key.format, (unsigned long long)key.hash,
        (unsigned long long)context.epoch, strict ? "selected" : "absent");
  if (!strict)
    return true;
  // [WP02B R10: bypass only this live source/metadata UV equality guard.]
  if (*fingerprint != surface_normals_.expected_mesh_uv_hash)
    return reject_surface_binding(
        "authored mesh_uv_hash does not match live source");
  if (context.model->count(0x10) != surface_normals_.expected_polygon_count)
    return reject_surface_binding("authored native polygon count mismatch");
  if (!surface_normals_.native_material_pack) {
    std::vector<std::string> names;
    for (std::uint32_t i = 0; i < context.model->count(0x10); ++i) {
      const auto *candidate = context.model->polygons[i].material;
      if (!candidate)
        return reject_surface_binding("native material is missing");
      if (!(candidate->flags & 1)) {
        names.emplace_back("untextured");
        continue;
      }
      if (!candidate->texture || !std::memchr(candidate->texture->name, 0, 20))
        return reject_surface_binding(
            "native texture identity name is invalid");
      names.emplace_back(candidate->texture->name);
    }
    try {
      std::istringstream input(surface_normals_.native_material_bytes,
                               std::ios::binary);
      auto pack = mw34::materials::read_surface_materials(
          input, bridge_config_.native_material_asset_id,
          surface_normals_.expected_mesh_uv_hash, names);
      surface_normals_.native_material_pack =
          std::make_shared<mw34::materials::SurfaceMaterialPack>(
              std::move(pack));
    } catch (const std::runtime_error &error) {
      return reject_surface_binding(error.what());
    }
  }
  const auto &pack = *surface_normals_.native_material_pack;
  const auto record = pack.bindings.at(context.material.polygon);
  if (record == mw34::materials::no_surface_material)
    return true; // Global UV expectation still applies; absence of extra
                 // material parameters is not the normal catalog's no_normal
                 // exclusion.
  if (!std::memchr(material->texture->name, 0, 20) ||
      pack.records.at(record)->material_name != material->texture->name)
    return reject_surface_binding(
        "authored material name no longer matches source");
  return true;
}

std::array<float, 4>
native_surface_parameters(const mw34::materials::SurfaceKey &key, int texture) {
  if (!lighting_.normals || bridge_config_.native_material_sidecar.empty())
    return {};
  surface_normals_load();
  if (!native_surface_binding_matches(key, texture))
    return {};
  const auto &pack = *surface_normals_.native_material_pack;
  const auto index = pack.bindings.at(native_context_.material.polygon);
  if (index == mw34::materials::no_surface_material)
    return {};
  const auto &record = *pack.records.at(index);
  return {record.roughness, record.reflectivity, record.metallic, 1.f};
}

int surface_normal_index(const mw34::materials::SurfaceKey &input_key,
                         bool enabled = true, int texture_handle = -1) {
  if (!enabled || !surface_normals_enabled()) {
    return -1;
  }
  surface_normals_load();
  mw34::materials::SurfaceKey key = input_key;
  // [R10 / V2-02 FAULT INJECTION TARGET: key.hash ^= 1ULL;]
  const auto &cache = surface_normals_.cache;
  const auto authored = cache.bindings.find(key);
  if (authored != cache.bindings.end() && authored->second < 0)
    return -1;
  const bool candidate =
      cache.bindings.count(key) || cache.world_bindings.count(key);
  if (bridge_config_.trace_native_bindings && !candidate &&
      bridge_config_.native_material_sidecar.empty())
    native_surface_binding_matches(key, texture_handle);
  if (candidate && !native_surface_binding_matches(key, texture_handle))
    return -1;
  const int index = surface_normals_.cache.surface_normal_index(key, enabled);
  surface_normals_.peak_decoded_bytes = std::max(
      surface_normals_.peak_decoded_bytes, surface_normals_.cache.world_bytes);
  return index;
}

const mw34::materials::WorldNormalCache::TextureRecord &
surface_normal_image(int index) {
  surface_normals_.cache.require_image(index);
  return surface_normals_.cache.textures.at(static_cast<std::size_t>(index));
}

std::uint64_t resident_normal_decoded_bytes() const {
  return surface_normals_.cache.world_bytes;
}

const NativeDeferredBinding *
validated_deferred_binding(const mw34::materials::SurfaceKey &key, int texture) {
  const auto *binding = dynamic_cast<const NativeDeferredBinding *>(
      deferred_material_binding_.get());
  if (!binding || binding->owner.lock() != material_binding_owner_ ||
      binding->epoch != group_load_epoch_ || binding->texture != texture ||
      texture < 0 || std::size_t(texture) >= textures_.size() ||
      binding->version != textures_[texture].version || binding->key < key ||
      key < binding->key) {
    reject_surface_binding("stale or foreign deferred material admission");
    return nullptr;
  }
  return binding;
}

int deferred_surface_normal_index(const mw34::materials::SurfaceKey &key,
                                  int texture) {
  if (!deferred_material_binding_)
    return surface_normal_index(key, true, texture);
  const auto *binding = validated_deferred_binding(key, texture);
  return binding ? binding->normal_index : -1;
}

std::array<float, 4>
deferred_surface_parameters(const mw34::materials::SurfaceKey &key, int texture) {
  if (!deferred_material_binding_)
    return native_surface_parameters(key, texture);
  const auto *binding = validated_deferred_binding(key, texture);
  return binding ? binding->surface_parameters : std::array<float, 4>{};
}

bool authenticated_deferred_world_draw() const {
  const auto *binding = dynamic_cast<const NativeDeferredBinding *>(
      deferred_material_binding_.get());
  return binding && (binding->normal_index >= 0 ||
                     binding->surface_parameters[3] > .5f) &&
         binding->owner.lock() == material_binding_owner_ &&
         binding->epoch == group_load_epoch_ && binding->texture >= 0 &&
         std::size_t(binding->texture) < textures_.size() &&
         binding->version == textures_[binding->texture].version &&
         !(binding->key < textures_[binding->texture].surface_key) &&
         !(textures_[binding->texture].surface_key < binding->key);
}
