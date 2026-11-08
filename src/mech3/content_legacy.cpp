#include "mech3/content_legacy.h"
#include "mech3/content_base.h"
#include "platform/host_path.h"

#include <algorithm>
#include <set>

#include "content/legacy.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/mission.h"
#include "mech3/mods.h"

namespace mech3 {
namespace {
std::vector<ModIaWorld> g_ia_worlds;
struct Resource {
    uint64_t bytes;
    std::string sha256, owner, logical;
};
std::map<std::string, Resource> g_resources;
void verify_resource(const std::string &path) {
    const auto expected = g_resources.find(path);
    if (expected == g_resources.end())
        return;
    std::error_code error;
    const auto &value = expected->second;
    if (!cc::content::fs::is_regular_file(platform::host_path(cc::content::fs::u8path(path)),
                                          error) ||
        cc::content::fs::file_size(platform::host_path(cc::content::fs::u8path(path)), error) !=
            value.bytes ||
        cc::content::file_sha256(cc::content::fs::u8path(path)) != value.sha256)
        cc::content::fail("payload-integrity", "Managed legacy resource changed after resolution",
                          value.owner, {}, value.logical);
}
} // namespace
void validate_content_legacy(const cc::content::ResolvedContentSet &content) {
    using namespace cc::content;
    if (content.of_kind("legacy_layer").empty()) {
        if (content.lock.contains("legacy_resources") &&
            content.lock["legacy_resources"] != Json::array())
            fail("legacy-layer", "Resources cannot mount without a legacy layer");
        return;
    }
    // Rebuild claims from integrity tables. A schema-3 package cannot forge a
    // normalized layer to evade player-file protection or gameplay identity.
    auto checked = content;
    resolve_legacy_claims(
        checked, {{"resource_overrides", content.lock.value("resource_overrides", Json::array())}});
    if (!content.lock.contains("legacy_resources") ||
        checked.lock["legacy_resources"] != content.lock["legacy_resources"])
        fail("legacy-layer", "Resolved legacy resources do not match their package claims");
    std::set<std::string> folders;
    for (const auto *layer : content.of_kind("legacy_layer")) {
        for (auto field = layer->data.begin(); field != layer->data.end(); ++field)
            if (field.key() != "files" && field.key() != "source_format" &&
                field.key() != "ia_worlds")
                fail("legacy-layer", "Unsupported legacy layer field", layer->owner, layer->id,
                     field.key());
        auto entries = layer->data.value("ia_worlds", Json::array());
        if (!entries.is_array() || entries.size() > 1024)
            fail("legacy-world", "Invalid Instant Action world list", layer->owner, layer->id);
        for (const auto &entry : entries) {
            if (!entry.is_object() || entry.size() != 2 || !entry.contains("directory") ||
                !entry["directory"].is_string() || !entry.contains("name") ||
                !entry["name"].is_string())
                fail("legacy-world", "Instant Action entries require directory and name",
                     layer->owner, layer->id);
            const auto folder = entry["directory"].get<std::string>();
            if (!valid_id(folder) || folder.find('.') != std::string::npos ||
                entry["name"].get<std::string>().empty() ||
                entry["name"].get<std::string>().size() > 255 || !folders.insert(folder).second)
                fail("legacy-world",
                     "Instant Action entries require unique simple native folders and a display "
                     "name",
                     layer->owner, layer->id, folder);
            bool available = mission_dir_index(folder.c_str()) >= 0;
            for (const auto *world : content.of_kind("world"))
                if (world->behavior == "legacy-layer" &&
                    world->data.value("directory", std::string()) == folder)
                    available = true;
            if (!available)
                fail("legacy-world", "Instant Action entry has no registered world", layer->owner,
                     layer->id, folder);
        }
    }
}
void unmount_content_legacy() {
    clear_content_base_digest_cache();
    auto &mounts = zutl::mounts();
    for (const auto &root : mounts.managed_roots) {
        auto &mods = mounts.mods;
        mods.erase(std::remove(mods.begin(), mods.end(), root), mods.end());
    }
    mounts.managed_roots.clear();
    mounts.managed_files.clear();
    mounts.managed_search.clear();
    mounts.managed_read_check = nullptr;
    g_resources.clear();
    for (const auto &registered : g_ia_worlds) {
        auto &entries = mod_ia_worlds();
        auto entry = std::find_if(entries.begin(), entries.end(), [&](const ModIaWorld &value) {
            return value.folder == registered.folder && value.name == registered.name;
        });
        if (entry != entries.end())
            entries.erase(entry);
    }
    g_ia_worlds.clear();
}
void mount_content_legacy(const cc::content::ResolvedContentSet &content) {
    validate_content_legacy(content);
    unmount_content_legacy();
    auto &mounts = zutl::mounts();
    for (const auto &claim : content.lock.value("legacy_resources", cc::content::Json::array())) {
        const auto owner = claim.at("owner").get<std::string>();
        const auto path =
            content.resource(owner, claim.at("source").get<std::string>()).generic_u8string();
        const auto logical = claim.at("path").get<std::string>();
        if (!zutl::mod_may_serve(logical))
            cc::content::fail("protected-resource", "Managed overlay cannot provide a player file",
                              owner, {}, logical);
        mounts.managed_files[logical] = path;
        const auto package = std::find_if(content.packages.begin(), content.packages.end(),
                                          [&](const auto &package) { return package.id == owner; });
        const auto root = package->root.generic_u8string();
        const auto &integrity =
            package->manifest.at("files").at(claim.at("source").get<std::string>());
        g_resources[path] = {integrity.at("bytes").get<uint64_t>(), claim.at("sha256"), owner,
                             logical};
        if (std::find(mounts.managed_roots.begin(), mounts.managed_roots.end(), root) ==
            mounts.managed_roots.end()) {
            mounts.managed_roots.push_back(root);
            // Tag its archives as mod data for the existing bounded archive and loose
            // reader contracts. Only the chosen files, never this whole root, mount.
            mounts.mods.push_back(root);
        }
    }
    mounts.managed_read_check = verify_resource;
    for (const auto *layer : content.of_kind("legacy_layer"))
        for (const auto &entry : layer->data.value("ia_worlds", cc::content::Json::array())) {
            ModIaWorld world{entry.at("directory"), entry.at("name")};
            mod_ia_worlds().push_back(world);
            g_ia_worlds.push_back(std::move(world));
        }
}
} // namespace mech3
