// Resolve an optional catalog; the shared WP-01 cache validates and pages it.
// Missing optional content stays neutral. Explicit paths never silently fall
// back.
void surface_normals_load_world(mw34::materials::WorldNormalCache &cache) {
  bool selected = false;
  if (!bridge_config_.world_normal_manifest.empty()) {
    cache.load_world_manifest(
        std::filesystem::u8path(bridge_config_.world_normal_manifest));
    selected = true;
  }
  for (const auto &path : bridge_config_.environment_world_manifests) {
    cache.load_world_manifest(std::filesystem::u8path(path));
    selected = true;
  }
  if (selected)
    return;
  const std::string found = zutl::find("environment/world.txt");
  if (!found.empty()) {
    cache.load_world_manifest(std::filesystem::u8path(found));
    return;
  }
  const std::filesystem::path candidates[] = {"mods/environment/world.txt",
                                              "assets/environment/world.txt",
                                              "environment/world.txt"};
  for (const auto &candidate : candidates) {
    if (std::filesystem::is_regular_file(candidate)) {
      cache.load_world_manifest(candidate);
      return;
    }
  }
}
