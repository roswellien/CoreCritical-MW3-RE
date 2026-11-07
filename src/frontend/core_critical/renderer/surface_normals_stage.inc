// surface_normals_stage.inc
// Full-content material matching for original textured geometry on any map.

struct SurfaceNormalState {
  bool loaded{false};
  std::map<mw34::materials::SurfaceKey, int> bindings;
  std::set<mw34::materials::SurfaceKey> authored;

  struct Page {
    std::filesystem::path path;
    std::set<mw34::materials::SurfaceKey> keys;
    bool loaded{false};
    std::vector<int> normal_indices;
    std::map<mw34::materials::SurfaceKey, std::uint32_t> local_bindings;
    std::uint64_t pixel_bytes{0};
    std::uint64_t last_use{0};
    bool initialized{false};
  };

  std::vector<Page> pages;
  std::map<mw34::materials::SurfaceKey, size_t> world_bindings;
  std::uint64_t world_bytes{0};
  std::uint64_t world_clock{0};
  std::map<int, size_t> world_images;

  struct NormalImageRecord {
    std::string name;
    unsigned width{0}, height{0};
    std::uint64_t hash{0};
    std::vector<std::uint32_t> pixels32;
  };
  std::vector<NormalImageRecord> normal_textures;
} surface_normals_;

#include "world_normals_pages.inc"

bool surface_normals_enabled() const { return lighting_.normals; }

void surface_normals_load() {
  if (surface_normals_.loaded) {
    return;
  }
  const char *authored_path = std::getenv("MW34_SURFACE_NORMALS");
  if (authored_path && *authored_path) {
    for (const auto &cat_path :
         mw34::materials::surface_normal_catalog_paths(authored_path)) {
      std::ifstream file(cat_path, std::ios::binary);
      if (!file) {
        throw std::runtime_error("Configured surface normal page is missing");
      }
      auto parsed = mw34::materials::read_surface_normals(file);
      const size_t base_idx = surface_normals_.normal_textures.size();
      for (const auto &[key, idx] : parsed.bindings) {
        surface_normals_.authored.insert(key);
        surface_normals_.bindings[key] =
            (idx == mw34::materials::no_normal) ? -1 : int(base_idx + idx);
      }
      for (auto &img : parsed.pack.images) {
        SurfaceNormalState::NormalImageRecord rec{};
        rec.name = std::move(img.name);
        rec.width = img.width;
        rec.height = img.height;
        rec.hash = img.hash;
        rec.pixels32 = std::move(img.pixels);
        surface_normals_.normal_textures.push_back(std::move(rec));
      }
    }
  }
  surface_normals_load_world();
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
  const auto it = surface_normals_.bindings.find(key);
  if (it != surface_normals_.bindings.end()) {
    if (it->second >= 0) {
      surface_normals_require_image(it->second);
    }
    return it->second;
  }
  const auto w_it = surface_normals_.world_bindings.find(key);
  if (w_it != surface_normals_.world_bindings.end()) {
    surface_normals_load_page(w_it->second);
    const auto loaded_it = surface_normals_.bindings.find(key);
    if (loaded_it != surface_normals_.bindings.end()) {
      return loaded_it->second;
    }
    return -1;
  }
  return -1;
}

const SurfaceNormalState::NormalImageRecord &surface_normal_image(int index) {
  surface_normals_require_image(index);
  return surface_normals_.normal_textures.at(size_t(index));
}

std::uint64_t resident_normal_decoded_bytes() const {
  return surface_normals_.world_bytes;
}
