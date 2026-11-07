#include "content/world.h"

#include <algorithm>
#include <initializer_list>

namespace cc::content {
namespace {
void fields(const Definition &definition, const Json &object,
            std::initializer_list<const char *> allowed, const std::string &path) {
    if (!object.is_object())
        fail("world-schema", "Expected an object", definition.owner, definition.id, path);
    for (const auto &field : object.items())
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const char *key) { return field.key() == key; }))
            fail("unsupported-world-field", "Unsupported authored world parameter",
                 definition.owner, definition.id, path + "." + field.key());
}
} // namespace
void validate_compiled_world_fields(const Json &world) {
    Definition definition{"world", world.value("id", std::string{}), {}, "compiled", {}, -1};
    fields(definition, world,
           {"format", "schema", "id", "digest", "bounds", "spawn", "chunks", "nodes", "markers",
            "textures", "readers"},
           "world");
    if (world.contains("chunks"))
        for (size_t i = 0; i < world["chunks"].size(); ++i) {
            const auto &chunk = world["chunks"][i];
            const auto path = "world.chunks[" + std::to_string(i) + "]";
            fields(definition, chunk,
                   {"name", "instance", "node", "vertices", "triangles", "uvs", "color", "alpha",
                    "soil", "texture", "image", "collision", "ground", "visible"},
                   path);
            for (const char *key : {"collision", "ground", "visible"})
                if (chunk.contains(key) && !chunk[key].is_boolean())
                    fail("world-schema", "Compiled mesh flags must be boolean", {}, definition.id,
                         path + "." + key);
        }
    if (world.contains("nodes")) {
        if (!world["nodes"].is_array())
            fail("world-schema", "Compiled nodes must be an array");
        for (size_t i = 0; i < world["nodes"].size(); ++i)
            fields(definition, world["nodes"][i],
                   {"id", "instance", "parent", "name", "local_matrix"},
                   "world.nodes[" + std::to_string(i) + "]");
    }
    if (world.contains("textures"))
        for (const auto &image : world["textures"].items())
            fields(definition, image.value(), {"width", "height", "pixels", "alpha"},
                   "world.textures." + image.key());
}
void validate_world_fields(const Definition &definition) {
    const auto &data = definition.data;
    if (definition.kind == "world" && definition.behavior == "legacy") {
        fields(definition, data, {"directory", "name"}, "data");
        return;
    }
    if (definition.kind == "world" && definition.behavior == "compiled") {
        fields(definition, data, {"name", "resource"}, "data");
        return;
    }
    if (definition.kind == "world_asset")
        fields(definition, data, {"name", "source", "meshes", "markers"}, "data");
    else if (definition.kind == "world" && definition.behavior == "authored")
        fields(definition, data,
               {"name", "base", "bounds", "spawn", "readers", "source", "meshes", "markers",
                "heightfield", "instances"},
               "data");
    else
        return;
    if (data.contains("markers")) {
        if (!data["markers"].is_array())
            fail("world-schema", "Markers must be an array", definition.owner, definition.id,
                 "data.markers");
        for (size_t i = 0; i < data["markers"].size(); ++i)
            fields(definition, data["markers"][i], {"name", "position"},
                   "data.markers[" + std::to_string(i) + "]");
    }
    if (data.contains("meshes")) {
        if (!data["meshes"].is_array())
            fail("world-schema", "Meshes must be an array", definition.owner, definition.id,
                 "data.meshes");
        for (size_t i = 0; i < data["meshes"].size(); ++i)
            fields(definition, data["meshes"][i],
                   {"vertices", "triangles", "uvs", "albedo", "color", "alpha", "soil", "texture",
                    "collision", "ground", "visible"},
                   "data.meshes[" + std::to_string(i) + "]");
    }
    if (data.contains("heightfield"))
        fields(definition, data["heightfield"],
               {"heights", "source", "spacing", "height_scale", "origin", "color", "soil"},
               "data.heightfield");
    if (data.contains("instances")) {
        if (!data["instances"].is_array())
            fail("world-schema", "Instances must be an array", definition.owner, definition.id,
                 "data.instances");
        for (size_t i = 0; i < data["instances"].size(); ++i) {
            const auto &instance = data["instances"][i];
            const auto path = "data.instances[" + std::to_string(i) + "]";
            if (data.contains("base")) {
                fields(definition, instance,
                       {"id", "asset", "matrix", "translation", "rotation", "scale", "collision",
                        "ground", "visible", "soil", "texture", "color", "alpha", "remove"},
                       path);
                if (instance.contains("remove") && !instance["remove"].is_boolean())
                    fail("world-schema", "Variant removal must be boolean", definition.owner,
                         definition.id, path + ".remove");
                if (instance.value("remove", false))
                    fields(definition, instance, {"id", "remove"}, path);
            } else {
                fields(definition, instance,
                       {"id", "asset", "matrix", "translation", "rotation", "scale", "collision",
                        "ground", "visible", "soil", "texture", "color", "alpha"},
                       path);
            }
        }
    }
}
} // namespace cc::content
