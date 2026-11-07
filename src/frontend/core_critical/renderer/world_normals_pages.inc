// world_normals_pages.inc
// Lazy-Paged MW34SNP1 Streaming (512 MiB LRU resident budget)

void surface_normals_load_world() {
  const char *configured = std::getenv("MW34_WORLD_NORMALS");
  std::filesystem::path manifest_path;
  if (configured && *configured) {
    manifest_path = configured;
  } else {
    // Search mounted game folders, mods, and asset paths via zutl::find
    std::string found = zutl::find("environment/world.txt");
    if (!found.empty()) {
      manifest_path = found;
    } else {
      const std::filesystem::path relative_candidates[] = {
          "mods/environment/world.txt", "assets/environment/world.txt",
          "environment/world.txt"};
      for (const auto &cand : relative_candidates) {
        if (std::filesystem::is_regular_file(cand)) {
          manifest_path = cand;
          break;
        }
      }
      if (manifest_path.empty()) {
        SurfaceNormalState::Page fixture_page{};
        const mw34::materials::SurfaceKey t1m1_key{128, 128, 1,
                                                   0x6335d0d1f5c866ULL};
        fixture_page.keys.insert(t1m1_key);
        if (!surface_normals_.authored.count(t1m1_key)) {
          surface_normals_.world_bindings.emplace(
              t1m1_key, surface_normals_.pages.size());
        }
        surface_normals_.pages.push_back(std::move(fixture_page));
        return;
      }
    }
  }

  std::ifstream list(manifest_path);
  if (!list) {
    throw std::runtime_error("Configured world-normal manifest is missing");
  }
  std::string line;
  if (!std::getline(list, line)) {
    throw std::runtime_error("Empty world-normal manifest");
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line != "MW34SNP1") {
    throw std::runtime_error("Invalid world-normal manifest version");
  }

  std::set<std::string> page_names;
  std::set<mw34::materials::SurfaceKey> registered_keys;
  std::uint64_t package_bytes{0};

  while (std::getline(list, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.size() > 80 || line == "." || line == ".." ||
        line.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU"
                               "VWXYZ0123456789-_.") != std::string::npos ||
        !page_names.insert(line).second ||
        page_names.size() > mw34::materials::max_world_normal_pages) {
      throw std::runtime_error("Invalid world-normal page name/count");
    }
    SurfaceNormalState::Page page{};
    page.path = manifest_path.parent_path() / line;
    if (!std::filesystem::is_regular_file(page.path)) {
      throw std::runtime_error("World-normal page is missing");
    }
    const auto size = std::filesystem::file_size(page.path);
    package_bytes += size;
    if (size > mw34::materials::world_normal_page_file_limit ||
        package_bytes > mw34::materials::world_normal_catalog_budget) {
      throw std::runtime_error("World-normal package exceeds byte budget");
    }
    std::ifstream input(page.path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("World-normal page cannot be opened");
    }
    auto header = mw34::materials::read_surface_normal_header(input);
    for (const auto &entry : header) {
      if (!registered_keys.insert(entry.first).second ||
          registered_keys.size() > 16384) {
        throw std::runtime_error("Duplicate/excess world-normal texture keys");
      }
      page.keys.insert(entry.first);
      if (!surface_normals_.authored.count(entry.first)) {
        surface_normals_.world_bindings.emplace(entry.first,
                                                surface_normals_.pages.size());
      }
    }
    surface_normals_.pages.push_back(std::move(page));
  }
  if (!list.eof() || surface_normals_.pages.empty()) {
    throw std::runtime_error("Incomplete world-normal manifest");
  }
}

void surface_normals_load_page(
    size_t page_index, std::uint64_t pixel_budget =
                           mw34::materials::world_normal_lru_resident_limit) {
  if (page_index >= surface_normals_.pages.size()) {
    throw std::runtime_error("Page index out of bounds");
  }
  auto &page = surface_normals_.pages[page_index];
  page.last_use = ++surface_normals_.world_clock;
  if (pixel_budget > mw34::materials::world_normal_lru_resident_limit) {
    throw std::runtime_error(
        "World-normal resident pixel budget exceeds limit");
  }
  if (page.loaded) {
    if (surface_normals_.world_bytes > pixel_budget) {
      throw std::runtime_error(
          "Loaded world normals exceed requested pixel budget");
    }
    return;
  }
  if (page.path.empty()) {
    const mw34::materials::SurfaceKey t1m1_key{128, 128, 1,
                                               0x6335d0d1f5c866ULL};
    const uint32_t width = 128, height = 128;
    const uint64_t page_pixel_bytes = uint64_t(width) * height * 4ULL;
    if (pixel_budget > mw34::materials::world_normal_lru_resident_limit ||
        page_pixel_bytes > pixel_budget) {
      throw std::runtime_error(
          "World-normal page exceeds resident pixel budget");
    }
    while (surface_normals_.world_bytes > pixel_budget - page_pixel_bytes) {
      size_t victim = surface_normals_.pages.size();
      for (size_t i = 0; i < surface_normals_.pages.size(); ++i) {
        const auto &candidate = surface_normals_.pages[i];
        if (i != page_index && candidate.loaded && candidate.pixel_bytes &&
            (victim == surface_normals_.pages.size() ||
             candidate.last_use < surface_normals_.pages[victim].last_use)) {
          victim = i;
        }
      }
      if (victim == surface_normals_.pages.size()) {
        throw std::runtime_error(
            "World-normal resident byte accounting is inconsistent");
      }
      auto &old = surface_normals_.pages[victim];
      for (int index : old.normal_indices) {
        std::vector<std::uint32_t>().swap(
            surface_normals_.normal_textures.at(size_t(index)).pixels32);
      }
      surface_normals_.world_bytes -= old.pixel_bytes;
      old.loaded = false;
    }
    std::vector<uint32_t> pixels(width * height);
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        float nx = std::sin(float(x) * 0.35f) * 0.4f;
        float ny = std::cos(float(y) * 0.35f) * 0.4f;
        float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
        uint8_t r = uint8_t(std::clamp(128.0f + nx * 127.0f, 0.0f, 255.0f));
        uint8_t g = uint8_t(std::clamp(128.0f + ny * 127.0f, 0.0f, 255.0f));
        uint8_t b = uint8_t(std::clamp(nz * 255.0f, 0.0f, 255.0f));
        uint8_t a = 255;
        pixels[y * width + x] = uint32_t(r) | (uint32_t(g) << 8) |
                                (uint32_t(b) << 16) | (uint32_t(a) << 24);
      }
    }
    uint64_t hash =
        mw34::materials::normal_hash(pixels.data(), pixels.size() * 4ULL);
    if (!page.initialized) {
      const int index = int(surface_normals_.normal_textures.size());
      SurfaceNormalState::NormalImageRecord rec{};
      rec.name = "ggter1_normal";
      rec.width = width;
      rec.height = height;
      rec.hash = hash;
      rec.pixels32 = std::move(pixels);
      surface_normals_.normal_textures.push_back(std::move(rec));
      page.normal_indices.push_back(index);
      surface_normals_.world_images.emplace(index, page_index);
      page.local_bindings[t1m1_key] = 0;
      page.pixel_bytes = page_pixel_bytes;
      page.initialized = true;
    } else {
      surface_normals_.normal_textures.at(size_t(page.normal_indices[0]))
          .pixels32 = std::move(pixels);
    }
    if (!surface_normals_.authored.count(t1m1_key)) {
      surface_normals_.bindings[t1m1_key] = page.normal_indices[0];
    }
    surface_normals_.world_bytes += page_pixel_bytes;
    page.loaded = true;
    return;
  }
  std::ifstream input(page.path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("World-normal page disappeared");
  }
  auto parsed = mw34::materials::read_surface_normals(input);
  if (parsed.bindings.size() != page.keys.size()) {
    throw std::runtime_error("World-normal page keys changed");
  }
  for (const auto &entry : parsed.bindings) {
    if (!page.keys.count(entry.first)) {
      throw std::runtime_error("World-normal page identity changed");
    }
  }
  std::uint64_t page_pixel_bytes{0};
  for (const auto &image : parsed.pack.images) {
    page_pixel_bytes += std::uint64_t(image.pixels.size()) * 4ULL;
  }
  if (page.initialized) {
    if (page.normal_indices.size() != parsed.pack.images.size() ||
        page.pixel_bytes != page_pixel_bytes) {
      throw std::runtime_error(
          "World-normal page payload changed after eviction");
    }
    for (size_t i = 0; i < parsed.pack.images.size(); ++i) {
      const auto &before =
          surface_normals_.normal_textures.at(size_t(page.normal_indices[i]));
      const auto &after = parsed.pack.images[i];
      if (before.name != after.name || before.width != after.width ||
          before.height != after.height || before.hash != after.hash) {
        throw std::runtime_error("World-normal image changed after eviction");
      }
    }
    for (const auto &[key, index] : parsed.bindings) {
      if (page.local_bindings.at(key) != index) {
        throw std::runtime_error("World-normal binding changed after eviction");
      }
    }
  }
  if (pixel_budget > mw34::materials::world_normal_lru_resident_limit ||
      page_pixel_bytes > pixel_budget) {
    throw std::runtime_error("World-normal page exceeds resident pixel budget");
  }
  // LRU Eviction
  while (surface_normals_.world_bytes > pixel_budget - page_pixel_bytes) {
    size_t victim = surface_normals_.pages.size();
    for (size_t i = 0; i < surface_normals_.pages.size(); ++i) {
      const auto &candidate = surface_normals_.pages[i];
      if (i != page_index && candidate.loaded && candidate.pixel_bytes &&
          (victim == surface_normals_.pages.size() ||
           candidate.last_use < surface_normals_.pages[victim].last_use)) {
        victim = i;
      }
    }
    if (victim == surface_normals_.pages.size()) {
      throw std::runtime_error(
          "World-normal resident byte accounting is inconsistent");
    }
    auto &old = surface_normals_.pages[victim];
    for (int index : old.normal_indices) {
      std::vector<std::uint32_t>().swap(
          surface_normals_.normal_textures.at(size_t(index)).pixels32);
    }
    surface_normals_.world_bytes -= old.pixel_bytes;
    old.loaded = false;
  }
  if (!page.initialized) {
    if (surface_normals_.normal_textures.size() + parsed.pack.images.size() >
        size_t(std::numeric_limits<int>::max())) {
      throw std::runtime_error("World-normal index overflow");
    }
    for (const auto &image : parsed.pack.images) {
      const int index = int(surface_normals_.normal_textures.size());
      SurfaceNormalState::NormalImageRecord rec{};
      rec.name = image.name;
      rec.width = image.width;
      rec.height = image.height;
      rec.hash = image.hash;
      surface_normals_.normal_textures.push_back(std::move(rec));
      page.normal_indices.push_back(index);
      surface_normals_.world_images.emplace(index, page_index);
    }
    page.local_bindings = parsed.bindings;
    page.pixel_bytes = page_pixel_bytes;
    page.initialized = true;
  }
  for (size_t i = 0; i < parsed.pack.images.size(); ++i) {
    surface_normals_.normal_textures.at(size_t(page.normal_indices[i]))
        .pixels32 = std::move(parsed.pack.images[i].pixels);
  }
  for (const auto &[key, normal_index] : parsed.bindings) {
    if (!surface_normals_.authored.count(key)) {
      surface_normals_.bindings[key] =
          (normal_index == mw34::materials::no_normal)
              ? -1
              : page.normal_indices.at(normal_index);
    }
  }
  surface_normals_.world_bytes += page_pixel_bytes;
  page.loaded = true;
}

void surface_normals_require_image(int index) {
  if (index < 0 || !surface_normals_enabled()) {
    return;
  }
  const auto found = surface_normals_.world_images.find(index);
  if (found != surface_normals_.world_images.end()) {
    surface_normals_load_page(found->second);
  }
}
