#include "mech3/content_world_hierarchy.h"

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include <cstring>

namespace mech3 {
namespace {
using cc::content::fail;
using cc::content::Json;
} // namespace
zcls::Node *ContentHierarchy::chunk_parent(const Json &chunk) const {
    if (chunk.contains("node")) {
        const auto found = nodes.find(chunk.at("node").get<std::string>());
        if (found == nodes.end())
            fail("world-node-binding", "Native chunk binding is unavailable");
        return found->second;
    }
    return instances.at(chunk.value("instance", std::string("terrain")));
}
ContentHierarchy build_content_hierarchy(const Json &world, zcls::Node *root,
                                         bool separate_instances) {
    ContentHierarchy result;
    if (separate_instances && !world.value("nodes", Json::array()).empty()) {
        root->content_hierarchy_parent = true;
        root->content_hierarchy_root = true;
    }
    auto instance = [&](const std::string &name) {
        if (result.instances.count(name))
            return;
        auto *parent = root;
        if (separate_instances) {
            parent = zcls::object3d_create();
            zcls::node_set_name(parent, name.c_str());
            parent->set_flags(parent->flags() & ~0x1000000u);
            zcls::world_add_child(root, parent);
        }
        result.instances.emplace(name, parent);
    };
    for (const auto &chunk : world.at("chunks"))
        instance(chunk.value("instance", std::string("terrain")));
    for (const auto &entry : world.value("nodes", Json::array()))
        instance(entry.at("instance").get<std::string>());
    for (const auto &marker : world.value("markers", Json::array()))
        instance(marker.at("instance").get<std::string>());
    for (const auto &entry : world.value("nodes", Json::array())) {
        auto *node = zcls::object3d_create();
        zcls::node_set_name(node, entry.at("name").get_ref<const std::string &>().c_str());
        node->set_flags(node->flags() & ~0x1000000u);
        const auto &matrix = entry.at("local_matrix");
        float native[12];
        // glTF column vectors and native row vectors store the same axis triples.
        // Removing each column's affine padding converts layouts without changing
        // multiplication order: native traversal composes local * parent.
        for (size_t axis = 0; axis < 4; ++axis)
            for (size_t row = 0; row < 3; ++row)
                native[axis * 3 + row] = matrix[axis * 4 + row].get<float>();
        zcls::object3d_set_matrix(node, native);
        result.nodes.emplace(entry.at("id").get<std::string>(), node);
        node->content_hierarchy_parent = true;
    }
    for (const auto &entry : world.value("nodes", Json::array())) {
        auto *node = result.nodes.at(entry.at("id").get<std::string>());
        auto *parent = entry.at("parent").is_null()
                           ? result.instances.at(entry.at("instance").get<std::string>())
                           : result.nodes.at(entry.at("parent").get<std::string>());
        zcls::object3d_add_child(parent, node);
        parent->content_hierarchy_parent = true;
        if (entry.at("parent").is_null())
            parent->content_hierarchy_root = true;
        zcls::update_node(node);
    }
    return result;
}
void apply_content_chunk_transform(zcls::Node *node, const Json &chunk) {
    if (chunk.value("reflection", false))
        zcls::object3d_set_scale(node, -1.0f, 1.0f, 1.0f);
}
bool content_vehicle_marker(zcls::Node *root, const char *name, zcls::Node **result) {
    if (!root || root->cls() != 5 || !root->content_hierarchy_root || !name)
        return false;
    const size_t size = std::strlen(name);
    const bool support = size == 9 && std::strncmp(name, "support", 7) == 0 && name[7] == '0' &&
                         name[8] >= '0' && name[8] <= '3';
    const bool collide = size == 9 && std::strncmp(name, "collide", 7) == 0 && name[7] >= '0' &&
                         name[7] <= '1' && name[8] >= '0' && name[8] <= '9' &&
                         (name[7] == '0' || name[8] <= '1');
    if (!support && !collide)
        return false;
    return content_hierarchy_child(root, name, result);
}
bool content_hierarchy_root(const char *name, zcls::Node **result) {
    *result = nullptr;
    if (!name)
        return false;
    bool imported = false;
    for (auto *item = zcls::list_head(zcls::kAllNodes); item; item = item->next) {
        auto *node = item->node;
        if (!node || (node->cls() != 5 && node->cls() != 2) || !node->data ||
            std::strcmp(node->name(), name) != 0)
            continue;
        if (node->content_hierarchy_root) {
            if (*result)
                fail("world-node-path",
                     "Managed instance root name is ambiguous; use a unique instance/actor name");
            *result = node;
        } else if (node->content_hierarchy_parent)
            imported = true;
    }
    if (*result)
        return true;
    if (imported)
        fail("world-node-path",
             "Imported node paths must begin with their asset instance or actor root");
    return false;
}
bool content_hierarchy_child(zcls::Node *parent, const char *name, zcls::Node **result) {
    if (!parent || (parent->cls() != 5 && parent->cls() != 2) || !parent->data ||
        !parent->content_hierarchy_parent)
        return false;
    *result = nullptr;
    if (!name)
        return true;
    for (int32_t i = 0; i < parent->child_count(); ++i)
        if (std::strcmp(parent->children[i]->name(), name) == 0) {
            *result = parent->children[i];
            break;
        }
    return true;
}
} // namespace mech3
