#include "mech3/content_world_state.h"

#include "content/content.h"
#include "crt/msvc_string.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zsave/zsave.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/content_registry.h"
#include "mech3/content_world.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mech3 {
namespace {
using cc::content::Json;
using cc::content::fail;
constexpr size_t kMaxNodes = 131072;
constexpr size_t kMaxRecord = 16 * 1024 * 1024;
constexpr const char* kEntry = "ContentWorld/Activation";

struct BoundNode {
    zcls::Node* node = nullptr;
    uint64_t allocation_id = 0;
};

std::string g_digest;
std::map<std::string, BoundNode> g_nodes;

void live_bindings() {
    std::set<const zcls::Node*> live;
    for (auto* item = zcls::list_head(zcls::kAllNodes); item; item = item->next)
        if (item->node && !item->removable) live.insert(item->node);
    for (const auto& binding : g_nodes) {
        auto* n = binding.second.node;
        if (!live.count(n) || n->cls() != 5 || n->allocation_id != binding.second.allocation_id)
            fail("save-world-state", "A persistent static node was deleted or changed class", {}, {}, binding.first);
    }
}

void validate(const Json& state, bool restoring) {
    if (!state.is_object() || state.size() != 5 ||
        state.value("format", std::string{}) != "ContentWorldActivation" ||
        !state.contains("schema") || !state["schema"].is_number_integer() || state["schema"] != 1 ||
        state.value("world_digest", std::string{}) != g_digest || g_digest.empty() ||
        !state.contains("deformed") || !state["deformed"].is_boolean() ||
        !state.contains("nodes") || !state["nodes"].is_object() ||
        state["nodes"].size() != g_nodes.size() || state["nodes"].size() > kMaxNodes)
        fail("save-world-state", "Authored activation snapshot version, world or node set differs");
    for (const auto& binding : g_nodes)
        if (!state["nodes"].contains(binding.first) || !state["nodes"][binding.first].is_boolean())
            fail("save-world-state", "Authored activation snapshot has an unknown or invalid node", {}, {}, binding.first);
    if (restoring && state["deformed"].get<bool>())
        fail("save-world-deformation", "This runtime cannot restore authored deformation history");
    live_bindings();
}

bool persistent_mission() {
    const auto content = active_content();
    const auto* mission = content ? content->find("mission", content_selected_mission()) : nullptr;
    return !mission || mission->data.value("persist_world", false);
}

void save_activation(zsave::Context& context, void*) {
    if (g_digest.empty()) return; // A previously registered callback must leave stock saves unchanged.
    const bool deformed = !zdec::craters().history.empty();
    if (deformed && persistent_mission())
        fail("save-world-deformation", "Persistent authored saves do not support deformation history");
    Json nodes = Json::object();
    live_bindings();
    for (const auto& binding : g_nodes)
        nodes[binding.first] = (binding.second.node->flags() & 4) != 0;
    const Json state = {{"format", "ContentWorldActivation"}, {"schema", 1},
                        {"world_digest", g_digest}, {"deformed", deformed}, {"nodes", std::move(nodes)}};
    const auto bytes = state.dump();
    if (bytes.size() > kMaxRecord)
        fail("save-world-state", "Authored activation snapshot exceeds its byte limit");
    zsave::write(context, "Activation", bytes.data(), static_cast<uint32_t>(bytes.size()));
}

Json decode(const void* data, size_t size) {
    if (!data || !size || size > kMaxRecord)
        fail("save-world-state", "Authored activation record exceeds its byte limit");
    return cc::content::parse_json(std::string(static_cast<const char*>(data), size), kEntry);
}

void load_activation(zsave::Context&, const char* name, const void* data, uint32_t size, void*) {
    if (std::strcmp(name, "Activation") != 0)
        fail("save-world-state", "Unsupported authored world state record");
    const auto state = decode(data, size);
    validate(state, true);
    // No allocation, reader lookup, model replay or remaining validation occurs
    // after this point. The complete class-5 identity set has been checked.
    for (const auto& binding : g_nodes)
        zcls::node_set_active(binding.second.node, state["nodes"][binding.first].get<bool>() ? 1 : 0);
}

struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return crt::msvc_stricmp(a.c_str(), b.c_str()) < 0;
    }
};

struct EntryRange {
    uint64_t start = 0;
    uint64_t end = 0;
};
} // namespace

bool content_world_activation_registered() {
    if (auto* m = zsave::manager()) {
        for (const auto& r : m->list)
            if (std::strcmp(r.name, "ContentWorld") == 0) return true;
    }
    return false;
}

void register_content_world_activation() {
    if (!g_digest.empty()) zsave::add("ContentWorld", save_activation, load_activation, 1000, nullptr);
}

void unregister_content_world_activation() {
    if (auto* m = zsave::manager()) {
        m->list.remove_if([](const zsave::Registrant& r) {
            return std::strcmp(r.name, "ContentWorld") == 0;
        });
    }
}

void bind_content_world_activation(const std::string& digest, std::map<std::string, zcls::Node*> nodes) {
    if (digest.empty() || nodes.size() > kMaxNodes)
        fail("save-world-state", "Authored activation identity table exceeds its limit");
    std::set<const zcls::Node*> live;
    for (auto* item = zcls::list_head(zcls::kAllNodes); item; item = item->next)
        if (item->node && !item->removable) live.insert(item->node);
    std::map<std::string, BoundNode> staged;
    for (const auto& binding : nodes) {
        if (binding.first.empty() || binding.first.size() > 1024 || !binding.second ||
            !live.count(binding.second) || binding.second->cls() != 5)
            fail("save-world-state", "Invalid authored activation identity", {}, {}, binding.first);
        staged.emplace(binding.first, BoundNode{binding.second, binding.second->allocation_id});
    }
    g_digest = digest;
    g_nodes = std::move(staged);
    register_content_world_activation();
}

void clear_content_world_activation() {
    unregister_content_world_activation();
    g_nodes.clear();
    g_digest.clear();
}

void validate_content_world_snapshot(const std::vector<uint8_t>& bytes, bool restoring) {
    if (g_digest.empty()) return;
    uint32_t footer[2]{};
    if (bytes.size() < 8) fail("save-world-state", "Authored native archive is truncated");
    std::memcpy(footer, bytes.data() + bytes.size() - 8, 8);
    if (footer[0] != 1 || !footer[1] || footer[1] > 65536 ||
        uint64_t(footer[1]) * sizeof(zutl_zar::Entry) > bytes.size() - 8)
        fail("save-world-state", "Authored native archive table is invalid");
    const size_t table = bytes.size() - 8 - size_t(footer[1]) * sizeof(zutl_zar::Entry);
    int records = 0, headers = 0;
    std::set<std::string, CaseInsensitiveLess> entry_names;
    std::vector<EntryRange> ranges;
    ranges.reserve(footer[1]);

    for (uint32_t i = 0; i < footer[1]; ++i) {
        zutl_zar::Entry entry{};
        std::memcpy(&entry, bytes.data() + table + i * sizeof entry, sizeof entry);
        if (!std::memchr(entry.name, 0, sizeof entry.name) ||
            entry.start > 0x7fffffffu ||
            uint64_t(entry.start) + entry.length > table)
            fail("save-world-state", "Authored native archive entry is invalid");
        if (!entry_names.insert(entry.name).second)
            fail("save-world-state", "Duplicate authored native archive entry");
        if (entry.length > 0)
            ranges.push_back({entry.start, uint64_t(entry.start) + entry.length});
        if (std::strcmp(entry.name, "zSaveHeader") == 0) {
            uint32_t header[2]{};
            if (entry.length == 8) std::memcpy(header, bytes.data() + entry.start, 8);
            if (header[0] != 8 || header[1] != 2) fail("save-world-state", "Authored native save kind differs");
            ++headers;
        } else if (std::strcmp(entry.name, kEntry) == 0) {
            validate(decode(bytes.data() + entry.start, entry.length), restoring);
            ++records;
        } else if (std::strncmp(entry.name, "ContentWorld/", 13) == 0) {
            fail("save-world-state", "Unsupported authored world state record");
        } else {
            // Other native registrants require their own complete preflight before
            // this activation-only format can safely dispatch their callbacks.
            fail("save-world-state", "Unsupported authored snapshot record", {}, {}, entry.name);
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const EntryRange& a, const EntryRange& b) {
        return a.start < b.start;
    });
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].start < ranges[i - 1].end)
            fail("save-world-state", "Authored native archive entries overlap");
    }

    if (headers != 1 || records != 1)
        fail("save-world-state", "Authored snapshot requires one native header and activation record");
}
} // namespace mech3
