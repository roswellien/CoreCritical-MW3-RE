// Optional environment admission: bounded metadata and streaming file
// integrity.
#include "platform/prepared_environment_bank.hpp"
#include "platform/sha256.hpp"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <fstream>
#include <nlohmann-json/json.hpp>
#include <regex>
#include <sstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace platform::prepared {
namespace {
namespace efs = std::filesystem;
using EJson = nlohmann::json;
constexpr std::uint64_t env_json_limit = 2ULL * 1024 * 1024;
constexpr std::uint64_t env_total_limit = 16ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t env_world_limit = 4096ULL * 1024 * 1024;
constexpr std::uint64_t env_page_limit = 16ULL * 1024 * 1024;
[[noreturn]] void env_fail(const std::string &message) {
  throw std::runtime_error("Environment pack: " + message);
}
std::string env_lower(std::string text) {
  for (auto &c : text)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return text;
}
std::uint64_t env_uint(const EJson &j) {
  if (!j.is_number_integer() ||
      (j.is_number_integer() && !j.is_number_unsigned() &&
       j.get<std::int64_t>() < 0))
    env_fail("expected unsigned integer");
  return j.get<std::uint64_t>();
}
std::string env_text(const EJson &j) {
  if (!j.is_string())
    env_fail("expected string");
  const auto value = j.get<std::string>();
  if (value.empty() || value.find('\0') != std::string::npos)
    env_fail("empty/NUL string");
  return value;
}
std::string env_read_text(const efs::path &path) {
  if (efs::file_size(path) > env_json_limit)
    env_fail("metadata/text size limit");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    env_fail("cannot open " + path.u8string());
  // Bound the read itself: a file may grow after file_size was checked.
  std::string text(static_cast<std::size_t>(env_json_limit) + 1, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  const auto count = input.gcount();
  if (input.bad() || (input.fail() && !input.eof()))
    env_fail("text read failed");
  if (count > static_cast<std::streamsize>(env_json_limit))
    env_fail("text grew during read");
  text.resize(static_cast<std::size_t>(count));
  return text;
}
EJson env_json(const efs::path &path) {
  const auto text = env_read_text(path);
  std::vector<std::set<std::string>> keys;
  std::size_t events = 0;
  try {
    return EJson::parse(
        text, [&](int depth, EJson::parse_event_t event, EJson &value) {
          if (depth > 32 || ++events > 100000)
            env_fail("JSON complexity limit");
          if (event == EJson::parse_event_t::object_start)
            keys.emplace_back();
          if (event == EJson::parse_event_t::key &&
              !keys.back().insert(value.get<std::string>()).second)
            env_fail("duplicate JSON key");
          if (event == EJson::parse_event_t::object_end)
            keys.pop_back();
          return true;
        });
  } catch (const EJson::exception &error) {
    env_fail(std::string("invalid JSON: ") +
             std::string(error.what()).substr(0, 512));
  }
}
bool env_link(const efs::path &path) {
#if defined(_WIN32)
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return true;
#endif
  return efs::is_symlink(efs::symlink_status(path));
}
efs::path env_root(const std::string &name) {
  if (name.empty() || name.size() > 32768 ||
      name.find('\0') != std::string::npos)
    env_fail("invalid pack root");
  auto path = efs::absolute(efs::u8path(name)).lexically_normal();
  for (auto part = path; !part.empty();) {
    if (env_link(part))
      env_fail("linked pack root/ancestor");
    const auto parent = part.parent_path();
    if (parent == part)
      break;
    part = parent;
  }
  if (!efs::is_directory(path))
    env_fail("pack directory is missing");
  return efs::canonical(path);
}
efs::path env_member(const efs::path &root, const std::string &name) {
  if (name.empty() || name.size() > 240 || name.find('\\') != std::string::npos)
    env_fail("unsafe member path");
  efs::path path = root;
  std::istringstream parts(name);
  std::string part;
  if (name.back() == '/')
    env_fail("empty path component");
  while (std::getline(parts, part, '/')) {
    if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
        part.back() == ' ')
      env_fail("unsafe path component");
    for (unsigned char c : part)
      if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' ||
          c == '?' || c == '*')
        env_fail("unsafe path character");
    const auto base = env_lower(part.substr(0, part.find('.')));
    if (base == "con" || base == "conin$" || base == "conout$" ||
        base == "prn" || base == "aux" || base == "nul" ||
        (base.size() == 4 &&
         (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") &&
         base[3] >= '0' && base[3] <= '9') ||
        base == "com\xc2\xb9" || base == "com\xc2\xb2" ||
        base == "com\xc2\xb3" || base == "lpt\xc2\xb9" ||
        base == "lpt\xc2\xb2" || base == "lpt\xc2\xb3")
      env_fail("reserved member path");
    path /= efs::u8path(part);
    if (env_link(path))
      env_fail("linked pack member");
  }
  if (!efs::is_regular_file(path))
    env_fail("member is missing: " + name);
  const auto canonical = efs::canonical(path),
             relative = canonical.lexically_relative(root);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    env_fail("member escapes pack");
  return canonical;
}
const std::vector<std::string> env_regions = {"operation1", "operation2",
                                              "operation3", "operation4",
                                              "training",   "shared-mechs"};
std::set<std::string> env_banks(const std::string &region) {
  if (region == "shared-mechs")
    return {"zbd/rmechtex.zbd", "zbd/rmechtex16.zbd"};
  std::vector<std::string> dirs = {
      region == "training" ? "t1" : "c" + region.substr(9)};
  if (region == "operation4")
    dirs.push_back("c4b");
  std::set<std::string> result;
  for (const auto &dir : dirs)
    for (const char *name : {"rtexture", "rtexture2", "rtexture3", "texture",
                             "texture1", "texture2"})
      result.insert("zbd/" + dir + "/" + name + ".zbd");
  return result;
}
struct EnvPin {
  std::string path, digest, format;
  std::uint64_t bytes{};
};
struct EnvPack {
  std::string id, region, root, manifest_digest;
  EJson registry;
  std::map<std::string, EnvPin> pins;
};
float env_float(const EJson &j, double min_val, double max_val) {
  if (j.is_boolean() || !j.is_number())
    env_fail("expected finite number");
  const double value = j.get<double>();
  if (!std::isfinite(value) || value < min_val || value > max_val)
    env_fail("number out of range");
  return static_cast<float>(value);
}
bool env_region_mission(const std::string &region, const std::string &mission) {
  if (region.size() != 10 || region.substr(0, 9) != "operation")
    return false;
  const char op = region[9];
  return mission.size() == 4 && mission[0] == 'c' && mission[1] == op &&
         mission[2] == 'm' && mission[3] >= '1' &&
         mission[3] <= (op < '3' ? '4' : '6');
}
void env_lighting(const efs::path &path, const std::string &region,
                  EnvironmentSelection *selected = nullptr) {
  const auto lighting = env_json(path);
  if (!lighting.is_object() || env_uint(lighting.at("schema_version")) != 1 ||
      (lighting.contains("missions") && !lighting["missions"].is_object()) ||
      (lighting.contains("maps") && !lighting["maps"].is_object()))
    env_fail("invalid lighting object");
  if (!lighting.contains("missions"))
    return;
  for (const auto &item : lighting["missions"].items()) {
    if (!region.empty() && !env_region_mission(region, item.key()))
      env_fail("invalid lighting mission key: " + item.key());
    if (!item.value().is_object())
      env_fail("invalid lighting mission entry");
    const auto &entry = item.value();
    const bool has_rgb = entry.contains("ambient_color");
    const bool has_strength = entry.contains("ambient_strength");
    if (has_rgb != has_strength)
      env_fail("incomplete mission ambient override");
    if (has_rgb) {
      const auto &rgb_json = entry["ambient_color"];
      if (!rgb_json.is_array() || rgb_json.size() != 3)
        env_fail("invalid mission ambient_color");
      std::array<float, 3> rgb{};
      for (std::size_t i = 0; i < 3; ++i)
        rgb[i] = env_float(rgb_json[i], 0.0, 4.0);
      const float strength = env_float(entry["ambient_strength"], 0.0, 2.0);
      if (selected)
        selected->mission_ambient.emplace(item.key(),
                                          MissionAmbientOverride{rgb, strength});
    }
    const bool has_tint = entry.contains("sky_tint");
    const bool has_brightness = entry.contains("sky_brightness");
    if (has_tint != has_brightness)
      env_fail("incomplete mission sky override");
    if (has_tint) {
      const auto &tint_json = entry["sky_tint"];
      if (!tint_json.is_array() || tint_json.size() != 3)
        env_fail("invalid mission sky_tint");
      std::array<float, 3> tint{};
      for (std::size_t i = 0; i < 3; ++i)
        tint[i] = env_float(tint_json[i], 0.0, 4.0);
      const float brightness = env_float(entry["sky_brightness"], 0.0, 4.0);
      if (selected)
        selected->mission_sky.emplace(item.key(),
                                      MissionSkyOverride{tint, brightness});
    }
    const bool has_glow_color = entry.contains("sky_glow_color");
    const bool has_glow_strength = entry.contains("sky_glow_strength");
    const bool has_glow_focus = entry.contains("sky_glow_focus");
    if (has_glow_color != has_glow_strength ||
        has_glow_color != has_glow_focus)
      env_fail("incomplete mission sky glow override");
    if (has_glow_color) {
      const auto &glow_json = entry["sky_glow_color"];
      if (!glow_json.is_array() || glow_json.size() != 3)
        env_fail("invalid mission sky_glow_color");
      std::array<float, 3> glow_color{};
      for (std::size_t i = 0; i < 3; ++i)
        glow_color[i] = env_float(glow_json[i], 0.0, 4.0);
      const float glow_strength =
          env_float(entry["sky_glow_strength"], 0.0, 4.0);
      const float glow_focus = env_float(entry["sky_glow_focus"], 0.0, 20.0);
      if (selected)
        selected->mission_sky_glow.emplace(
            item.key(),
            MissionSkyGlowOverride{glow_color, glow_strength, glow_focus});
    }
    const bool has_horizon_low = entry.contains("horizon_low");
    const bool has_horizon_high = entry.contains("horizon_high");
    if (has_horizon_low != has_horizon_high)
      env_fail("incomplete mission horizon override");
    if (has_horizon_low) {
      const float horizon_low = env_float(entry["horizon_low"], 0.0, 0.9);
      const float horizon_high = env_float(entry["horizon_high"], 0.0, 1.0);
      const bool interior_sentinel =
          entry.contains("is_interior") &&
          entry["is_interior"].is_boolean() &&
          entry["is_interior"].get<bool>() && horizon_low == 0.0f &&
          horizon_high == 0.0f;
      if (!interior_sentinel) {
        if (!(horizon_low < horizon_high))
          env_fail("invalid mission horizon range");
        if (selected)
          selected->mission_horizon.emplace(
              item.key(), MissionHorizonOverride{horizon_low, horizon_high});
      }
    }
  }
}
void env_header(const EnvPin &pin) {
  std::ifstream input(efs::u8path(pin.path), std::ios::binary);
  char head[24]{};
  input.read(head, sizeof head);
  const auto count = input.gcount();
  const std::map<std::string, std::string> magic = {
      {"BWSNR2", std::string("BWSNR2\0\0", 8)},
      {"BWSNR3", std::string("BWSNR3\0\0", 8)},
      {"BWSFM1", std::string("BWSFM1\0\0", 8)},
      {"MWFX1", std::string("MWFX1\0\0\0", 8)},
      {"MW34SNA1", "MW34SNA1"},
      {"MW34SNP1", "MW34SNP1"}};
  if (pin.format == "ZBD-texture-v1") {
    detail::validate_environment_bank(efs::u8path(pin.path), pin.bytes);
  } else if (pin.format == "MW34-lighting-v1") {
    env_lighting(efs::u8path(pin.path), {});
  } else {
    const auto it = magic.find(pin.format);
    if (it == magic.end() || count < 8 ||
        std::memcmp(head, it->second.data(), 8))
      env_fail("unsupported/mislabeled resource format");
    if (pin.format == "BWSNR2" || pin.format == "BWSNR3") {
      input.clear();
      input.seekg(0, std::ios::beg);
      (void)mw34::materials::read_surface_normal_header(input);
    }
  }
}
void env_catalog(const EnvPack &pack, const char *role, const char *magic,
                 std::set<std::string> &references, std::uint64_t &bytes,
                 std::size_t &pages) {
  if (!pack.registry.contains(role) || !pack.registry[role].is_array())
    env_fail("normal page list is missing");
  const auto manifest =
      env_text(pack.registry.at(std::string(role) + "_manifest"));
  if (!pack.pins.count(manifest) || pack.pins.at(manifest).format != magic)
    env_fail("undeclared normal manifest");
  references.insert(manifest);
  std::string expected = std::string(magic) + "\n";
  if (pack.registry[role].empty())
    env_fail("empty normal page list");
  for (const auto &entry : pack.registry[role]) {
    const auto name = env_text(entry);
    const auto it = pack.pins.find(name);
    if (it == pack.pins.end() ||
        (it->second.format != "BWSNR2" &&
         (std::string(role) != "world" || it->second.format != "BWSNR3")))
      env_fail("undeclared/mislabeled normal page");
    if (!references.insert(name).second)
      env_fail("duplicate resource role");
    const auto relative =
        efs::u8path(name)
            .lexically_relative(efs::u8path(manifest).parent_path())
            .generic_u8string();
    if (relative.empty() || relative.find('/') != std::string::npos ||
        relative == ".." ||
        relative.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"
                                   "RSTUVWXYZ0123456789-_.") !=
            std::string::npos ||
        relative.size() > (std::string(role) == "world" ? 80U : 512U))
      env_fail("normal page outside manifest directory");
    expected += relative + "\n";
    bytes += it->second.bytes;
    ++pages;
    const bool world = std::string(role) == "world";
    if ((world && (it->second.bytes > env_page_limit ||
                   bytes > env_world_limit || pages > 256)) ||
        (!world && (bytes > 1536ULL * 1024 * 1024 || pages > 16)))
      env_fail("normal catalog budget exceeded");
  }
  auto actual = env_read_text(efs::u8path(pack.pins.at(manifest).path));
  std::string normalized;
  std::istringstream lines(actual);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    normalized += line + "\n";
  }
  actual = std::move(normalized);
  if (!actual.empty() && actual.back() != '\n')
    actual += '\n';
  if (actual != expected)
    env_fail("manifest differs from declared pages");
}
} // namespace

std::string environment_region(const std::string &mission) {
  if (mission.size() == 4 && mission[2] == 'm' && mission[3] >= '1' &&
      mission[3] <= '6') {
    if (mission.substr(0, 2) == "t1" && mission[3] <= '4')
      return "training";
    if (mission[0] == 'c' && mission[1] >= '1' && mission[1] <= '4' &&
        (mission[1] >= '3' || mission[3] <= '4'))
      return "operation" + mission.substr(1, 1);
  }
  if (mission.size() == 5 && mission.substr(0, 4) == "c4bm" &&
      mission[4] >= '4' && mission[4] <= '6')
    return "operation4";
  env_fail("unknown stock mission region");
}
EnvironmentSelection
load_environment_packs(const std::vector<std::string> &roots) {
  struct FloatScope {
    std::fenv_t saved{};
    FloatScope() {
      if (std::feholdexcept(&saved) != 0)
        env_fail("cannot preserve floating-point environment");
    }
    ~FloatScope() { std::fesetenv(&saved); }
  } preserve;
  if (roots.size() > 32)
    env_fail("too many pack roots");
  std::vector<EnvPack> packs;
  std::set<std::string> ids, regions, canonical_roots;
  integrity::Sha256 identity;
  auto fold = [&](const std::string &value) {
    const auto n = std::to_string(value.size()) + ":";
    identity.update(n.data(), n.size());
    identity.update(value.data(), value.size());
  };
  try {
    for (const auto &directory : roots) {
      const auto root = env_root(directory);
      EnvPack pack;
      pack.root = root.u8string();
      if (!canonical_roots.insert(pack.root).second)
        env_fail("duplicate pack root");
      const auto manifest_path = env_member(root, "pack.json");
      const auto manifest = env_json(manifest_path);
      pack.manifest_digest = integrity::sha256_file(manifest_path);
      pack.id = env_text(manifest.at("id"));
      if (!std::regex_match(pack.id,
                            std::regex("[a-z0-9][a-z0-9._-]{0,127}")) ||
          !ids.insert(pack.id).second)
        env_fail("invalid/duplicate pack ID");
      const auto &payload = manifest.at("native_payload");
      if (env_uint(manifest.at("schema_version")) != 2 ||
          payload.at("profile") != "prepared-environment-v1" ||
          payload.at("registry") != "environment/environment.json" ||
          payload.at("path_base") != "pack-root")
        env_fail("unsupported contract");
      if ((manifest.contains("dependencies") &&
           !manifest["dependencies"].empty()) ||
          (manifest.contains("conflicts") && !manifest["conflicts"].empty()))
        env_fail("unsupported dependencies/conflicts");
      if (payload.contains("required_capabilities"))
        for (const auto &cap : payload["required_capabilities"].items())
          if ((cap.key() != "environment-resources" &&
               cap.key() != "surface-materials") ||
              env_uint(cap.value()) != 1)
            env_fail("unsupported required capability");
      const std::set<std::string> formats = {
          "ZBD-texture-v1", "BWSNR2",   "BWSNR3",   "BWSFM1",
          "MWFX1",          "MW34SNA1", "MW34SNP1", "MW34-lighting-v1"};
      if (!payload.at("required_formats").is_array())
        env_fail("required formats must be an array");
      for (const auto &format : payload["required_formats"])
        if (!formats.count(env_text(format)))
          env_fail("unsupported required format");
      const auto &files = manifest.at("files");
      if (!files.is_object() || files.empty() || files.size() > 10000)
        env_fail("invalid file table");
      std::set<std::string> names = {"pack.json"};
      std::uint64_t total = 0;
      for (const auto &item : files.items()) {
        if (!names.insert(env_lower(item.key())).second)
          env_fail("case-colliding member name");
        EnvPin pin;
        pin.path = env_member(root, item.key()).u8string();
        pin.bytes = env_uint(item.value().at("bytes"));
        pin.digest = env_text(item.value().at("sha256"));
        if (pin.digest.size() != 64 ||
            !std::all_of(pin.digest.begin(), pin.digest.end(),
                         [](char c) {
                           return (c >= '0' && c <= '9') ||
                                  (c >= 'a' && c <= 'f');
                         }) ||
            pin.bytes > env_total_limit || total > env_total_limit - pin.bytes)
          env_fail("invalid integrity pin/budget");
        total += pin.bytes;
        if (efs::file_size(efs::u8path(pin.path)) != pin.bytes)
          env_fail("member size mismatch: " + item.key());
        if (item.value().contains("format")) {
          pin.format = env_text(item.value()["format"]);
          if (pin.format == "BWSNR3" && pin.bytes > env_page_limit)
            env_fail("world page byte limit");
          if (pin.format == "BWSFM1" &&
              (!payload.contains("required_capabilities") ||
               !payload["required_capabilities"].contains(
                   "surface-materials") ||
               env_uint(
                   payload["required_capabilities"]["surface-materials"]) != 1))
            env_fail("surface-materials capability required");
          if ((pin.format == "BWSFM1" || pin.format == "BWSNR2") &&
              pin.bytes > 256ULL * 1024 * 1024)
            env_fail("sidecar byte limit");
          env_header(pin);
        }
        pack.pins.emplace(item.key(), std::move(pin));
      }
      const auto registry = pack.pins.find("environment/environment.json");
      if (registry == pack.pins.end())
        env_fail("undeclared registry");
      pack.registry = env_json(efs::u8path(registry->second.path));
      pack.region = env_text(pack.registry.at("operation"));
      if (env_uint(pack.registry.at("schema_version")) != 2 ||
          pack.registry.at("payload_profile") != "prepared-environment-v1" ||
          std::find(env_regions.begin(), env_regions.end(), pack.region) ==
              env_regions.end())
        env_fail("unsupported registry");
      // [WP03B R10: bypass only duplicate region ownership rejection.]
      if (!env_claim_region(regions, pack.region))
        env_fail("conflicting region ownership: " + pack.region);

      packs.push_back(std::move(pack));
    }
    std::sort(packs.begin(), packs.end(), [](const auto &a, const auto &b) {
      return std::find(env_regions.begin(), env_regions.end(), a.region) <
             std::find(env_regions.begin(), env_regions.end(), b.region);
    });
    EnvironmentSelection selected;
    std::uint64_t world_bytes = 0, authored_bytes = 0;
    std::size_t world_pages = 0, authored_pages = 0;
    std::map<std::string, std::string> common;
    for (const auto &pack : packs) {
      std::set<std::string> refs = {"environment/environment.json"}, banks;
      for (const auto &entry : pack.registry.at("banks").items()) {
        banks.insert(entry.key());
        const auto name = env_text(entry.value());
        const auto pin = pack.pins.find(name);
        if (pin == pack.pins.end() || pin->second.format != "ZBD-texture-v1")
          env_fail("undeclared bank");
        refs.insert(name);
        selected.banks.emplace(entry.key(), pin->second.path);
      }
      if (banks != env_banks(pack.region))
        env_fail("incomplete/out-of-region banks");
      env_catalog(pack, "authored", "MW34SNA1", refs, authored_bytes,
                  authored_pages);
      selected.authored_manifests.push_back(
          pack.pins.at(env_text(pack.registry["authored_manifest"])).path);
      if (pack.region != "shared-mechs") {
        env_catalog(pack, "world", "MW34SNP1", refs, world_bytes, world_pages);
        selected.world_manifests.push_back(
            pack.pins.at(env_text(pack.registry["world_manifest"])).path);
        const auto &detail = pack.registry.at("detail");
        if (!detail.is_object() || detail.size() != 3 ||
            !detail.contains("water") || !detail.contains("surf") ||
            !detail.contains("cloud"))
          env_fail("missing detail resources");
        for (const auto &entry : detail.items()) {
          const auto name = env_text(entry.value());
          if (!pack.pins.count(name) || pack.pins.at(name).format != "BWSNR2")
            env_fail("invalid detail role");
          refs.insert(name);
          selected.resources[pack.region]
                            [entry.key() == "water"  ? EnvironmentRole::water
                             : entry.key() == "surf" ? EnvironmentRole::surf
                                                     : EnvironmentRole::cloud] =
              pack.pins.at(name).path;
          const auto &digest = pack.pins.at(name).digest;
          if (common.count(entry.key()) && common[entry.key()] != digest)
            env_fail("conflicting shared detail resource");
          common[entry.key()] = digest;
        }
        const auto name = env_text(pack.registry.at("effects"));
        if (!pack.pins.count(name) || pack.pins.at(name).format != "MWFX1")
          env_fail("invalid effects role");
        refs.insert(name);
        selected.resources[pack.region][EnvironmentRole::effects] =
            pack.pins.at(name).path;
        const auto &digest = pack.pins.at(name).digest;
        if (common.count("effects") && common["effects"] != digest)
          env_fail("conflicting shared effects");
        common["effects"] = digest;
      }
      for (const char *role :
           {"lighting", "ground_surface_materials", "surface_materials"})
        if (pack.registry.contains(role)) {
          const auto name = env_text(pack.registry[role]);
          if (!pack.pins.count(name) ||
              pack.pins.at(name).format != (std::string(role) == "lighting"
                                                ? "MW34-lighting-v1"
                                                : "BWSFM1"))
            env_fail("invalid optional resource role");
          refs.insert(name);
          if (std::string(role) == "lighting")
            env_lighting(efs::u8path(pack.pins.at(name).path), pack.region,
                         &selected);
          selected.resources[pack.region]
                            [std::string(role) == "lighting"
                                 ? EnvironmentRole::lighting
                             : std::string(role) == "ground_surface_materials"
                                 ? EnvironmentRole::ground_materials
                                 : EnvironmentRole::surface_materials] =
              pack.pins.at(name).path;
        }
      if (refs.size() != pack.pins.size())
        env_fail("unused environment member");
      selected.regions.push_back(pack.region);
    }
    // Shape, ownership and global budgets are admitted before expensive
    // hashing.
    for (const auto &pack : packs) {
      fold(pack.root);
      fold(pack.manifest_digest);
      for (const auto &member : pack.pins) {
        const auto &pin = member.second;
        const auto path = efs::u8path(pin.path);
        if (efs::file_size(path) != pin.bytes ||
            integrity::sha256_file(path) != pin.digest ||
            efs::file_size(path) != pin.bytes)
          env_fail("member integrity failure: " + member.first);
        fold(member.first);
        fold(pin.digest);
      }
    }
    selected.identity = roots.empty() ? std::string{} : identity.hex();
    return selected;
  } catch (const EJson::exception &error) {
    env_fail(std::string("schema mismatch: ") +
             std::string(error.what()).substr(0, 512));
  }
}
const EnvironmentSelection &
EnvironmentPackCache::resolve(const std::vector<std::string> &roots) {
  try {
    auto candidate = load_environment_packs(roots);
    if (candidate.identity == selection_.identity) {
      ++hits_;
      return selection_;
    }
    selection_ = std::move(candidate);
    ++generation_;
    return selection_;
  } catch (...) {
    selection_ = {};
    ++generation_;
    throw;
  }
}
} // namespace platform::prepared
