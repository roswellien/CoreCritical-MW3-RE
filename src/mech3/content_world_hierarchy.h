#pragma once

#include "content/content.h"
#include <map>

namespace zcls {
struct Node;
}
namespace mech3 {
struct ContentHierarchy {
    std::map<std::string, zcls::Node *> instances;
    std::map<std::string, zcls::Node *> nodes;
    zcls::Node *chunk_parent(const cc::content::Json &chunk) const;
};
// Per-instance roots stay identity so existing baked geometry/markers do not
// move twice. Imported scene roots carry instance placement in local_matrix.
ContentHierarchy build_content_hierarchy(const cc::content::Json &world, zcls::Node *root,
                                         bool separate_instances);
// Imported basenames require an explicit live instance/actor root first.
bool content_hierarchy_root(const char *name, zcls::Node **result);
// Managed hierarchy paths traverse immediate children. Legacy native lookups
// keep their original recursive semantics when this returns false.
bool content_hierarchy_child(zcls::Node *parent, const char *name, zcls::Node **result);
} // namespace mech3
