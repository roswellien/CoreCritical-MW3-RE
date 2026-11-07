// surface_normals_stage.inc
// Full-content material matching for original textured geometry on any map.

struct SurfaceNormalState {
  bool loaded{false};
  std::uint64_t peak_decoded_bytes{};
  mw34::materials::WorldNormalCache cache{};
} surface_normals_;

#include "world_normals_pages.inc"

bool surface_normals_enabled() const { return lighting_.normals; }

void surface_normals_load() {
  if (surface_normals_.loaded) {
    return;
  }
  // Transactional build: fill a local cache first and publish only on success,
  // so an exception leaves no partial entries behind for a subsequent attempt.
  mw34::materials::WorldNormalCache cache{};
  const char *authored_path = bridge_config_.surface_normal_catalog.c_str();
  if (authored_path && *authored_path) {
    for (const auto &cat_path :
         mw34::materials::surface_normal_catalog_paths(authored_path)) {
      std::ifstream file(cat_path, std::ios::binary);
      if (!file) {
        throw std::runtime_error("Configured surface normal page is missing");
      }
      auto parsed = mw34::materials::read_surface_normals(file);
      const std::size_t base_idx = cache.textures.size();
      for (const auto &[key, idx] : parsed.bindings) {
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
  surface_normals_.cache = std::move(cache);
  surface_normals_.loaded = true;
}

int surface_normal_index(const mw34::materials::SurfaceKey &input_key,
                         bool enabled = true) {
  if (!enabled || !surface_normals_enabled()) {
    return -1;
  }
  surface_normals_load();
  mw34::materials::SurfaceKey key = input_key;
  // [R10 / V2-02 FAULT INJECTION TARGET: key.hash ^= 1ULL;]
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
