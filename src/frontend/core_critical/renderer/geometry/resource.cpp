#include "resource.hpp"

namespace cc::geometry {
std::shared_ptr<const Resource> register_resource(const float *positions,
                                                  std::uint32_t position_count,
                                                  const float *normals, std::uint32_t normal_count,
                                                  const std::vector<Polygon> &polygons) {
    if (!positions || !position_count || position_count > 1'000'000 || polygons.size() > 1'000'000)
        throw std::runtime_error("Geometry registration exceeds supported source bounds");
    auto resource = std::make_shared<Resource>();
    resource->source_vertices = position_count;
    resource->source_polygons = std::uint32_t(polygons.size());
    static std::atomic<std::uint64_t> next_identity{1};
    auto identity = next_identity.load(std::memory_order_relaxed);
    do {
        if (identity >= (1ULL << 56))
            throw std::runtime_error("Geometry identity exhausted");
    } while (
        !next_identity.compare_exchange_weak(identity, identity + 1, std::memory_order_relaxed));
    resource->identity = identity;
    for (std::size_t polygon_index = 0; polygon_index < polygons.size(); ++polygon_index) {
        const auto &source = polygons[polygon_index];
        if (!source.positions || source.count < 3 || source.count > 64)
            throw std::runtime_error("Invalid geometry polygon");
        if (resource->vertices.size() > 1'000'000 - source.count)
            throw std::runtime_error("Geometry corner budget exceeded");
        const auto base = std::uint32_t(resource->vertices.size());
        // GameZ copies polygon corners in reverse order before drawing.
        for (std::uint32_t corner = source.count; corner-- > 0;) {
            const auto index = source.positions[corner];
            if (index >= position_count)
                throw std::runtime_error("Geometry position index outside resource");
            Vertex vertex{};
            vertex.x = positions[3 * index];
            vertex.y = positions[3 * index + 1];
            vertex.z = positions[3 * index + 2];
            if (source.uv) {
                vertex.u = source.uv[2 * corner];
                vertex.v = source.uv[2 * corner + 1];
            }
            if (source.normals) {
                const auto normal = source.normals[corner];
                if (!normals || normal >= normal_count)
                    throw std::runtime_error("Geometry normal index outside resource");
                vertex.nx = normals[3 * normal];
                vertex.ny = normals[3 * normal + 1];
                vertex.nz = normals[3 * normal + 2];
            }
            for (float value : {vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz,
                                vertex.u, vertex.v})
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite registered geometry");
            resource->vertices.push_back(vertex);
        }
        Range range{std::uint32_t(resource->indices.size()), 0, std::uint32_t(polygon_index),
                    source.two_sided || source.strip};
        for (std::uint32_t triangle = 2; triangle < source.count; ++triangle) {
            if (source.strip) {
                const auto first = triangle - 2;
                resource->indices.insert(resource->indices.end(),
                                         {base + first + (first & 1U),
                                          base + first + 1 - (first & 1U), base + triangle});
            } else {
                resource->indices.insert(resource->indices.end(),
                                         {base, base + triangle - 1, base + triangle});
            }
        }
        range.count = std::uint32_t(resource->indices.size()) - range.first;
        if (!source.normals) {
            std::array<float, 3> flat{};
            for (std::uint32_t index = range.first; index < range.first + range.count; index += 3) {
                const auto &a = resource->vertices[resource->indices[index]];
                const auto &b = resource->vertices[resource->indices[index + 1]];
                const auto &c = resource->vertices[resource->indices[index + 2]];
                const float ax = b.x - a.x, ay = b.y - a.y, az = b.z - a.z;
                const float bx = c.x - a.x, by = c.y - a.y, bz = c.z - a.z;
                const std::array<float, 3> normal{az * by - ay * bz, ax * bz - az * bx,
                                                  ay * bx - ax * by};
                if (source.strip) {
                    for (unsigned corner = 0; corner < 3; ++corner) {
                        auto &vertex = resource->vertices[resource->indices[index + corner]];
                        vertex.nx += normal[0];
                        vertex.ny += normal[1];
                        vertex.nz += normal[2];
                    }
                } else if (flat == std::array<float, 3>{}) {
                    flat = normal;
                }
            }
            for (std::uint32_t corner = base; corner < base + source.count; ++corner) {
                auto &vertex = resource->vertices[corner];
                if (!source.strip) {
                    vertex.nx = flat[0];
                    vertex.ny = flat[1];
                    vertex.nz = flat[2];
                }
                const float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny +
                                               vertex.nz * vertex.nz);
                if (!std::isfinite(length))
                    throw std::runtime_error("Geometry normal exceeds finite range");
                if (length > 0) {
                    vertex.nx /= length;
                    vertex.ny /= length;
                    vertex.nz /= length;
                } else
                    vertex.ny = 1; // Zero-area primitives rasterize no pixels.
            }
        }
        range.material_slot = source.material_slot == std::numeric_limits<std::uint32_t>::max()
                                  ? std::uint32_t(polygon_index)
                                  : source.material_slot;
        if (!resource->ranges.empty() &&
            resource->ranges.back().material_slot == range.material_slot &&
            resource->ranges.back().two_sided == range.two_sided) {
            // Only adjacent opaque polygons are merged: equal-depth ordering
            // and every material transition retain their original order.
            resource->ranges.back().count += range.count;
        } else {
            resource->ranges.push_back(range);
        }
    }
    return resource;
}

} // namespace cc::geometry
