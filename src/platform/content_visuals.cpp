#include "platform/content_visuals.h"

#include <set>

namespace platform::content {
namespace {
using cc::content::Json;
std::vector<PreparedAppearance>
selected_appearances(const cc::content::ResolvedContentSet &content,
                     const std::shared_ptr<const cc::content::ResolvedContentSet> &retained);
void reject(const cc::content::Definition &definition, const std::string &field,
            const std::string &message) {
    cc::content::fail("appearance", message, definition.owner, definition.id, field);
}
} // namespace

void register_capabilities(cc::content::Capabilities &capabilities) {
#if MW3_CONTENT_PREPARED_VISUALS
    capabilities["prepared-mech-visuals"] = 1;
#else
    (void)capabilities;
#endif
}

void validate(const cc::content::ResolvedContentSet &content) {
    for (const auto *definition : content.of_kind("appearance")) {
#if !MW3_CONTENT_PREPARED_VISUALS
        reject(*definition, "behavior", "This runtime has no prepared-mech visual backend");
#endif
        if (definition->behavior != "prepared-mech-v1" || !definition->data.is_object())
            reject(*definition, "behavior", "Unsupported prepared appearance contract");
        const auto &data = definition->data;
        for (const auto &field : data.items())
            if (!std::set<std::string>{"chassis", "lod", "mesh", "textures", "normals", "materials"}
                     .count(field.key()))
                reject(*definition, field.key(), "Unknown prepared appearance field");
        for (const auto *field : {"chassis", "lod", "mesh", "textures"})
            if (!data.contains(field) || !data.at(field).is_string() ||
                data.at(field).get<std::string>().empty())
                reject(*definition, field, "Required appearance field is missing");
        const auto *chassis = content.find("chassis", data.at("chassis").get<std::string>());
        if (!chassis)
            reject(*definition, "chassis", "Appearance chassis is unavailable");
        if (data.at("lod") != "0" && data.at("lod") != "1")
            reject(*definition, "lod", "Prepared appearances support LOD 0 or 1");
        for (const auto *field : {"mesh", "textures", "normals", "materials"}) {
            if (!data.contains(field))
                continue;
            if (!data.at(field).is_string())
                reject(*definition, field, "Expected an owned package resource path");
            const auto path =
                content.resource(definition->owner, data.at(field).get<std::string>());
            if (!cc::content::fs::is_regular_file(path))
                reject(*definition, field, "Appearance resource is missing");
        }
    }
    (void)selected_appearances(content, {});
}

namespace {
std::vector<PreparedAppearance>
selected_appearances(const cc::content::ResolvedContentSet &content,
                     const std::shared_ptr<const cc::content::ResolvedContentSet> &retained) {
    std::vector<PreparedAppearance> result;
    if (!content.lock.contains("selections") ||
        !content.lock.at("selections").contains("appearances"))
        return result;
    const auto &selected = content.lock.at("selections").at("appearances");
    if (!selected.is_array())
        cc::content::fail("appearance-selection", "Expected an array of appearance IDs");
    std::set<std::pair<std::string, std::string>> bound;
    for (const auto &id : selected) {
        if (!id.is_string())
            cc::content::fail("appearance-selection", "Expected an appearance ID");
        const auto *definition = content.find("appearance", id.get<std::string>());
        if (!definition)
            cc::content::fail("appearance-selection", "Selected appearance is unavailable", {},
                              id.get<std::string>());
        const auto &data = definition->data;
        const auto *chassis = content.find("chassis", data.at("chassis").get<std::string>());
        if (!chassis)
            reject(*definition, "chassis", "Selected appearance chassis is unavailable");
        if (!bound.emplace(chassis->id, data.at("lod").get<std::string>()).second)
            reject(*definition, "chassis", "Multiple appearances select the same chassis and LOD");
        PreparedAppearance binding;
        binding.content_id = chassis->id;
        binding.chassis = chassis->data.at("model_name").get<std::string>();
        binding.lod = data.at("lod").get<std::string>();
        std::map<std::string, std::string> references;
        for (const auto &field : {std::make_pair("mesh", &binding.mesh),
                                  {"textures", &binding.textures},
                                  {"normals", &binding.normals},
                                  {"materials", &binding.materials}})
            if (data.contains(field.first)) {
                *field.second =
                    content.resource(definition->owner, data.at(field.first).get<std::string>())
                        .u8string();
                references.emplace(*field.second, data.at(field.first).get<std::string>());
            }
        if (retained) {
            const auto owner = definition->owner;
            // Keep the exact resolved session alive until its last lazy visual read.
            // Paths alone lose the immutable package identity after selection.
            binding.validate_resource = [retained, owner, references](const std::string &path) {
                const auto reference = references.find(path);
                if (reference == references.end())
                    cc::content::fail("appearance-resource",
                                      "Visual payload is outside its selection", owner);
                if (retained->resource(owner, reference->second).u8string() != path)
                    cc::content::fail("appearance-resource", "Visual payload location changed",
                                      owner);
            };
        }
        result.push_back(std::move(binding));
    }
    return result;
}
} // namespace

std::vector<PreparedAppearance>
selections(std::shared_ptr<const cc::content::ResolvedContentSet> content) {
    if (!content)
        return {};
    return selected_appearances(*content, content);
}
} // namespace platform::content
