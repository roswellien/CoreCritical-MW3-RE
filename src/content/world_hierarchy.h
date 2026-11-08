#pragma once

#include "content/content.h"
#include <array>
#include <set>

namespace cc::content::hierarchy {
// The native matrix stack has 32 slots; leave room for view/world/actor/chunk.
constexpr size_t max_depth = 24;
constexpr size_t max_nodes = 65536;
using Matrix = std::array<double, 16>;
std::string append_node(Json &world, const std::string &instance, size_t source_index,
                        const Json &source, const std::string &parent, const Matrix &local);
// Validates the forest and all chunk bindings before native objects are allocated.
// Matrices use glTF column-major layout; bound chunk positions remain node-local.
std::set<std::string> validate(const Json &world);
} // namespace cc::content::hierarchy
