#pragma once

#include "frontend/core_critical/renderer/surface_materials.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Project-authored prepared visual payloads. These readers contain no assets
// and operate independently of SDL, Vulkan, game memory and the original file
// formats.
namespace platform::prepared {

using Matrix = std::array<double, 16>;
using Vertex = std::array<float, 8>; // position xyz, normal xyz, UV

struct Bone {
    std::string name, control;
    int32_t parent = -1;
    Matrix rest{};
};
struct Reference {
    std::string name;
    Matrix rest{};
};
struct Rig {
    bool ground_reference = false;
    double hip_drop = 0;
    std::array<double, 2> walk_x{}, walk_z{};
    std::vector<Bone> bones;
    std::vector<Reference> references;
};
struct Batch {
    std::string owner, visibility, placement_frame, joint_frame, anchor_control;
    std::array<float, 3> placement_offset{}, joint_offset{}, anchor_position{};
    uint32_t color = 0xffffffff;
    uint32_t donor_bone = 0xffffffff;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> donor_indices;
};
struct Mesh {
    unsigned version = 0;
    Rig rig;
    std::vector<Batch> batches;
};
struct Image {
    std::string name;
    unsigned width = 0, height = 0;
    uint64_t hash = 0;
    std::vector<uint16_t> pixels; // RGB565; alpha belongs to the source material
};
struct Textures {
    std::vector<uint32_t> bindings;
    std::vector<Image> images;
};

constexpr uint32_t no_normal = 0xffffffff;
struct NormalImage {
    std::string name;
    unsigned width = 0, height = 0;
    uint64_t hash = 0;
    std::vector<uint32_t> pixels; // linear RGBA8; alpha retains its authored meaning
};
struct Normals {
    std::vector<uint32_t> bindings; // per batch, image index or explicit no_normal
    std::vector<NormalImage> images;
};

Mesh read_mesh(std::istream &input);
Textures read_textures(std::istream &input);
Normals read_normals(std::istream &input, const Textures &textures);
void validate_bindings(const Mesh &mesh, const Textures &textures);
mw34::materials::SurfaceMaterialPack
validate_surface_materials(std::istream &input, const std::string &expected_asset_id,
                           uint64_t expected_mesh_uv_hash,
                           const std::vector<std::string> &expected_batch_materials);

// Owned, validated optional pack resources. Selection never enables a stage.
enum class EnvironmentRole {
  water,
  surf,
  cloud,
  effects,
  lighting,
  ground_materials,
  surface_materials
};
struct MissionAmbientOverride {
  std::array<float, 3> color{};
  float strength{};
};
struct MissionSkyOverride {
  std::array<float, 3> tint{};
  float brightness{};
};
struct MissionSkyGlowOverride {
  std::array<float, 3> color{};
  float strength{};
  float focus{};
};
struct MissionHorizonOverride {
  float low{};
  float high{};
};
struct EnvironmentSelection {
  std::map<std::string, std::string> banks;
  std::map<std::string, std::map<EnvironmentRole, std::string>> resources;
  std::map<std::string, MissionAmbientOverride> mission_ambient;
  std::map<std::string, MissionSkyOverride> mission_sky;
  std::map<std::string, MissionSkyGlowOverride> mission_sky_glow;
  std::map<std::string, MissionHorizonOverride> mission_horizon;
  std::vector<std::string> authored_manifests, world_manifests, regions;
  std::string identity;
};
EnvironmentSelection
load_environment_packs(const std::vector<std::string> &roots);
std::string environment_region(const std::string &mission);
// Revalidates content before a hit; corruption cannot reuse an older admission.
class EnvironmentPackCache {
public:
  const EnvironmentSelection &resolve(const std::vector<std::string> &roots);
  std::uint64_t hits() const { return hits_; }
  std::uint64_t generation() const { return generation_; }

private:
  EnvironmentSelection selection_;
  std::uint64_t hits_{}, generation_{};
};

} // namespace platform::prepared
