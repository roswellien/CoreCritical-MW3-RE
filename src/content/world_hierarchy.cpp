#include "content/world_hierarchy.h"

#include <cmath>
#include <functional>
#include <limits>
#include <map>

namespace cc::content::hierarchy {
namespace {
bool native_name(const std::string &name) {
    if (name.empty() || name.size() > 35)
        return false;
    for (unsigned char value : name)
        if (value < 32 || value == 127)
            return false;
    return true;
}
Matrix read_matrix(const Json &value) {
    if (!value.is_array() || value.size() != 16)
        fail("world-node-matrix", "Node local_matrix requires sixteen column-major values");
    Matrix matrix{};
    for (size_t i = 0; i < 16; ++i) {
        if (!value[i].is_number())
            fail("world-node-matrix", "Node matrices require finite numbers");
        matrix[i] = value[i].get<double>();
        if (!std::isfinite(matrix[i]) || std::abs(matrix[i]) > 10000000)
            fail("world-node-matrix", "Node matrix exceeds native precision bounds");
    }
    if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1)
        fail("world-node-matrix", "Node matrices must be affine");
    const double determinant = matrix[0] * (matrix[5] * matrix[10] - matrix[9] * matrix[6]) -
                               matrix[4] * (matrix[1] * matrix[10] - matrix[9] * matrix[2]) +
                               matrix[8] * (matrix[1] * matrix[6] - matrix[5] * matrix[2]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        fail("world-node-matrix", "Node matrix must be nonsingular");
    return matrix;
}
Matrix multiply(const Matrix &parent, const Matrix &local) {
    Matrix result{};
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row)
            for (size_t inner = 0; inner < 4; ++inner)
                result[column * 4 + row] += parent[inner * 4 + row] * local[column * 4 + inner];
    for (double value : result)
        if (!std::isfinite(value) || std::abs(value) > 10000000)
            fail("world-node-matrix", "Composed node transform exceeds native precision bounds");
    return result;
}
std::array<double, 3> transformed(const Matrix &matrix, const Json &vertex) {
    if (!vertex.is_array() || vertex.size() != 3)
        fail("world-node-vertex", "Bound vertices require three coordinates");
    std::array<double, 3> result{};
    for (size_t i = 0; i < 3; ++i) {
        if (!vertex[i].is_number() || !std::isfinite(vertex[i].get<double>()) ||
            std::abs(vertex[i].get<double>()) > 10000000)
            fail("world-node-vertex", "Node-local vertex exceeds native precision bounds");
    }
    for (size_t row = 0; row < 3; ++row) {
        result[row] = matrix[row] * vertex[0].get<double>() +
                      matrix[4 + row] * vertex[1].get<double>() +
                      matrix[8 + row] * vertex[2].get<double>() + matrix[12 + row];
        if (!std::isfinite(result[row]) || std::abs(result[row]) > 10000000)
            fail("world-node-vertex", "Transformed vertex exceeds native coordinate bounds");
    }
    return result;
}
struct Node {
    std::string parent, instance, name;
    Matrix local, world;
    size_t depth = 0;
    int state = 0;
};
} // namespace
std::string append_node(Json &world, const std::string &instance, size_t source_index,
                        const Json &source, const std::string &parent, const Matrix &local) {
    const auto name = source.value("name", "node_" + std::to_string(source_index));
    if (!native_name(name))
        fail("gltf-node-name",
             "Imported node names must fit 35 native bytes without control characters");
    if (world["nodes"].size() >= max_nodes)
        fail("world-node-limit", "World hierarchy exceeds 65536 nodes");
    const auto id = instance + "/gltf/" + std::to_string(source_index);
    world["nodes"].push_back({{"id", id},
                              {"instance", instance},
                              {"parent", parent.empty() ? Json(nullptr) : Json(parent)},
                              {"name", name},
                              {"local_matrix", local}});
    return id;
}
std::set<std::string> validate(const Json &world) {
    const Json entries = world.value("nodes", Json::array());
    if (!entries.is_array() || entries.size() > max_nodes)
        fail("world-node-limit", "World nodes require a bounded array");
    std::map<std::string, Node> nodes;
    std::map<std::pair<std::string, std::string>, std::set<std::string>> names;
    std::set<std::string> instances;
    for (const auto &entry : entries) {
        if (!entry.is_object() || entry.size() != 5 || !entry.contains("id") ||
            !entry["id"].is_string() || !entry.contains("instance") ||
            !entry["instance"].is_string() || !entry.contains("parent") ||
            (!entry["parent"].is_null() && !entry["parent"].is_string()) ||
            !entry.contains("name") || !entry["name"].is_string() ||
            !entry.contains("local_matrix"))
            fail("world-node",
                 "Compiled nodes require exactly id, instance, parent, name and local_matrix");
        const auto id = entry["id"].get<std::string>(),
                   instance = entry["instance"].get<std::string>(),
                   name = entry["name"].get<std::string>();
        const auto parent =
            entry["parent"].is_null() ? std::string{} : entry["parent"].get<std::string>();
        if (id.empty() || id.size() > 128 || id.find('\0') != std::string::npos ||
            !native_name(instance) || !native_name(name) ||
            (!entry["parent"].is_null() && parent.empty()))
            fail("world-node-name",
                 "Node identities and native names must be bounded and nonempty");
        if (!nodes.emplace(id, Node{parent, instance, name, read_matrix(entry["local_matrix"]), {}})
                 .second)
            fail("world-node-id", "Duplicate compiled node identity");
        if (!names[{instance, parent}].insert(name).second)
            fail("world-node-name", "Ambiguous sibling node names are unsupported");
        instances.insert(instance);
    }
    std::function<void(const std::string &, size_t)> resolve = [&](const std::string &id,
                                                                   size_t budget) {
        if (budget > max_depth)
            fail("world-node-depth", "World hierarchy exceeds 24 retained node levels");
        auto &node = nodes.at(id);
        if (node.state == 1)
            fail("world-node-cycle", "World hierarchy contains a cycle");
        if (node.state == 2)
            return;
        node.state = 1;
        if (node.parent.empty()) {
            node.world = node.local;
            node.depth = 1;
        } else {
            const auto found = nodes.find(node.parent);
            if (found == nodes.end() || found->second.instance != node.instance)
                fail("world-node-parent",
                     "Node parent is missing or belongs to another asset instance");
            resolve(node.parent, budget + 1);
            node.depth = found->second.depth + 1;
            if (node.depth > max_depth)
                fail("world-node-depth", "World hierarchy exceeds 24 retained node levels");
            node.world = multiply(found->second.world, node.local);
        }
        node.state = 2;
    };
    for (const auto &entry : nodes)
        resolve(entry.first, 1);
    size_t index = 0;
    for (const auto &chunk : world.at("chunks")) {
        const auto instance = chunk.value("instance", std::string("terrain"));
        const bool bound = chunk.contains("node");
        const auto id = bound ? chunk.at("node").get<std::string>() : std::string{};
        const auto generated = (bound ? "__cc_mesh_" : "cc_") + std::to_string(index++);
        if (!names[{instance, id}].insert(generated).second)
            fail("world-node-name",
                 "Imported node name collides with a generated native mesh node");
        if (!bound)
            continue;
        const auto found = nodes.find(id);
        if (found == nodes.end() || found->second.instance != instance)
            fail("world-node-binding",
                 "Chunk node binding is missing or belongs to another instance");
        for (const auto &vertex : chunk.at("vertices"))
            transformed(found->second.world, vertex);
        for (const auto &triangle : chunk.at("triangles")) {
            const auto a =
                transformed(found->second.world, chunk["vertices"][triangle[0].get<size_t>()]);
            const auto b =
                transformed(found->second.world, chunk["vertices"][triangle[1].get<size_t>()]);
            const auto c =
                transformed(found->second.world, chunk["vertices"][triangle[2].get<size_t>()]);
            const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0],
                         vy = c[1] - a[1], vz = c[2] - a[2];
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            if (nx * nx + ny * ny + nz * nz < 1e-16)
                fail("world-node-degenerate", "Node transform collapses a triangle");
        }
    }
    for (const auto &marker : world.value("markers", Json::array())) {
        if (!marker.contains("instance") || !marker.contains("name"))
            continue;
        if (!names[{marker.at("instance").get<std::string>(), {}}]
                 .insert(marker.at("name").get<std::string>())
                 .second)
            fail("world-node-name", "Marker collides with an imported root node");
    }
    return instances;
}
} // namespace cc::content::hierarchy
