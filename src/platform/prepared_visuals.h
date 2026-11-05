#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

// Project-authored prepared visual payloads. These readers contain no assets and
// operate independently of SDL, Vulkan, game memory and the original file formats.
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

Mesh read_mesh(std::istream &input);
Textures read_textures(std::istream &input);
void validate_bindings(const Mesh &mesh, const Textures &textures);

} // namespace platform::prepared
