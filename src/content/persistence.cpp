#include "content/persistence.h"
#include "content/hash.h"
#include "platform/host_path.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cc::content {
namespace {
constexpr uint64_t kWorldLimit = 512ull * 1024 * 1024;
std::string hash_world(const std::vector<uint8_t> &bytes) {
    Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    return hash.finish();
}
void put64(std::ostream &stream, uint64_t word) {
    for (unsigned i = 0; i < 8; ++i)
        stream.put(static_cast<char>(word >> (i * 8)));
}
uint64_t get64(std::istream &stream) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        int byte = stream.get();
        if (byte < 0)
            fail("save-truncated", "Truncated bundle header");
        value |= uint64_t(byte) << (i * 8);
    }
    return value;
}
struct Bundle {
    Json metadata;
    SavedState payload;
};
Bundle read_bundle(const fs::path &path) {
    std::ifstream input(platform::host_path(path), std::ios::binary);
    char magic[8]{};
    if (!input.read(magic, 8) || std::string(magic, 8) != "CCSAVE1\n")
        fail("save-format", "Unsupported content save bundle");
    uint64_t metadata_size = get64(input), world_size = get64(input);
    if (metadata_size > 16 * 1024 * 1024 || world_size > kWorldLimit)
        fail("save-limit", "Content save exceeds supported limits");
    std::string expected(64, '\0');
    if (!input.read(expected.data(), 64))
        fail("save-truncated", "Missing bundle integrity header");
    std::string metadata(static_cast<size_t>(metadata_size), '\0');
    if (!input.read(metadata.data(), static_cast<std::streamsize>(metadata_size)) ||
        sha256(metadata) != expected)
        fail("save-integrity", "Save metadata is truncated or corrupt");
    Bundle bundle;
    bundle.metadata = parse_json(metadata, "save bundle");
    if (!bundle.metadata.contains("schema_version") ||
        !bundle.metadata["schema_version"].is_number_integer() ||
        bundle.metadata["schema_version"] != 1)
        fail("save-schema", "Unsupported save schema");
    bundle.payload.state = bundle.metadata.at("state");
    bundle.payload.world.resize(static_cast<size_t>(world_size));
    if (!input.read(reinterpret_cast<char *>(bundle.payload.world.data()),
                    static_cast<std::streamsize>(world_size)) ||
        input.peek() != std::char_traits<char>::eof() ||
        hash_world(bundle.payload.world) != bundle.metadata.at("world_sha256"))
        fail("save-integrity", "World snapshot is truncated, corrupt or has trailing bytes");
    return bundle;
}
void migrate_identity(Json &value, const std::string &kind, const Json &ids,
                      const ResolvedContentSet &resolved) {
    if (value.is_null())
        return;
    if (!value.is_string())
        fail("migration-identity", "Expected a typed stable identity", {}, {}, kind);
    const std::string previous = value.get<std::string>();
    auto target = ids.contains(previous) ? ids.at(previous).get<std::string>() : previous;
    const bool stock_skin =
        kind == "skin" && target.rfind("stock:", 0) == 0 && target.find('.') != std::string::npos;
    if (!resolved.find(kind, target) && !stock_skin)
        fail("migration-type", "Final saved identity does not exist in the required content kind",
             {}, target, kind);
    value = target;
}
void migrate_state(Json &value, const Json &ids, const ResolvedContentSet &resolved) {
    if (value.is_array()) {
        for (auto &child : value)
            migrate_state(child, ids, resolved);
        return;
    }
    if (!value.is_object())
        return;
    if (value.contains("kind") && value.contains("id")) {
        std::string kind;
        if (value["kind"].is_number_integer()) {
            if (value["kind"] < 0 || value["kind"] > 4)
                fail("migration-type", "Unsupported saved item kind");
            switch (value["kind"].get<int>()) {
            case 0:
                kind = "chassis";
                break;
            case 1:
            case 2:
                kind = "weapon";
                break;
            case 3:
                kind = "equipment";
                break;
            case 4:
                break;
            default:
                fail("migration-type", "Unsupported saved item kind");
            }
        } else if (value["kind"].is_string()) {
            kind = value["kind"].get<std::string>();
            if (kind == "ammo")
                kind = "weapon";
        }
        if (!kind.empty())
            migrate_identity(value["id"], kind, ids, resolved);
    }
    for (const char *kind : {"campaign", "mission", "chassis", "skin"})
        if (value.contains(kind)) {
            if (std::string(kind) == "campaign" && value[kind] == "")
                continue;
            if (std::string(kind) == "skin" && value[kind] == "" && value.contains("chassis") &&
                value["chassis"].is_null())
                continue;
            migrate_identity(value[kind], kind, ids, resolved);
        }
    if (value.contains("snapshot_mission") && value["snapshot_mission"] != "")
        migrate_identity(value["snapshot_mission"], "mission", ids, resolved);
    if (value.contains("mission_items") && value["mission_items"].is_object()) {
        Json items = Json::object();
        for (auto it = value["mission_items"].begin(); it != value["mission_items"].end(); ++it) {
            Json id = it.key();
            migrate_identity(id, "mission_item", ids, resolved);
            if (items.contains(id.get<std::string>()))
                fail("migration-collision", "Mission item mappings collide");
            items[id.get<std::string>()] = it.value();
        }
        value["mission_items"] = std::move(items);
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        // Arbitrary author/player strings remain opaque. Only structured semantic
        // records are traversed; variable names and descriptions are never ID aliases.
        if (it.key() == "variables" || it.key() == "name" || it.key() == "label" ||
            it.key() == "description" || it.key() == "notes")
            continue;
        if (it.value().is_structured())
            migrate_state(it.value(), ids, resolved);
    }
}
const Json &campaign_node(const Definition &campaign, const std::string &id) {
    const auto &nodes = campaign.data.at("nodes");
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 1024)
        fail("campaign-state", "Target campaign has no bounded node graph");
    for (const auto &node : nodes)
        if (node.value("id", std::string{}) == id)
            return node;
    fail("campaign-state", "Saved campaign references an unknown target node", {}, campaign.id, id);
}
} // namespace
std::string new_campaign_run_id(const std::string &campaign) {
    static std::atomic<uint64_t> serial{0};
    const auto time = std::chrono::system_clock::now().time_since_epoch().count();
    const auto monotonic = std::chrono::steady_clock::now().time_since_epoch().count();
    return sha256("cc-campaign-run-v1\n" + campaign + "\n" + std::to_string(time) + "\n" +
                  std::to_string(monotonic) + "\n" + std::to_string(++serial));
}
bool valid_campaign_run_id(const std::string &run) {
    return run.size() == 64 && run.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void validate_saved_campaign_graph(const Json &state, const ResolvedContentSet &resolved) {
    const auto campaign_id = state.value("campaign", std::string{});
    if (campaign_id.empty())
        return;
    const auto *campaign = resolved.find("campaign", campaign_id);
    if (!campaign)
        fail("campaign-state", "Saved campaign is unavailable", {}, campaign_id);
    const auto &graph = state.at("graph");
    if (!graph.is_object() || !graph.contains("schema") || !graph["schema"].is_number_integer() ||
        graph["schema"] != 1 || graph.value("campaign", std::string{}) != campaign_id ||
        !graph.contains("terminal") || !graph["terminal"].is_boolean() || !graph.contains("run") ||
        !graph["run"].is_string() ||
        !valid_campaign_run_id(graph["run"].get_ref<const std::string &>()))
        fail("campaign-state", "Saved campaign progression identity or schema is invalid");
    const auto mission_id = state.at("mission").get<std::string>();
    if (!resolved.find("mission", mission_id))
        fail("campaign-state", "Saved campaign mission is unavailable", {}, mission_id);
    if (campaign_id == "stock:campaign" && campaign->behavior == "legacy") {
        if (!graph.contains("stock_operation") || !graph["stock_operation"].is_number_integer() ||
            graph["stock_operation"] < 1 || graph["stock_operation"] > 4 ||
            !graph.contains("stock_mission") || !graph["stock_mission"].is_number_integer())
            fail("campaign-state", "Saved stock campaign identity is invalid");
        const int operation = graph["stock_operation"].get<int>();
        const int last_mission = operation < 3 ? 4 : 6;
        if (graph["stock_mission"] < 1 || graph["stock_mission"] > last_mission)
            fail("campaign-state", "Saved stock campaign mission is outside its operation");
        const auto expected = "stock:c" + std::to_string(operation) + "m" +
                              std::to_string(graph["stock_mission"].get<int>());
        if (mission_id != expected)
            fail("campaign-state", "Saved stock campaign progression selects another mission");
        return;
    }
    if (campaign->behavior != "graph" || !graph.contains("node") || !graph["node"].is_string() ||
        !graph.contains("attempt") || !graph["attempt"].is_number_integer() ||
        graph["attempt"] < 1 || graph["attempt"] > INT64_MAX)
        fail("campaign-state", "Saved graph campaign node or attempt is invalid");
    const auto &node = campaign_node(*campaign, graph["node"].get<std::string>());
    if (node.at("mission") != mission_id)
        fail("campaign-state", "Saved graph node does not select its declared mission");
    const auto tokens = graph.value("mission_items", Json::object());
    if (!tokens.is_object() || tokens.size() > 65536)
        fail("campaign-state", "Saved mission item inventory is malformed");
    for (auto token = tokens.begin(); token != tokens.end(); ++token)
        if (!resolved.find("mission_item", token.key()) || !token.value().is_number_integer() ||
            token.value() < 0 || token.value() > INT32_MAX)
            fail("campaign-state", "Saved mission item is unavailable or has an invalid count");
    const auto variables = graph.value("variables", Json::object());
    if (!variables.is_object())
        fail("campaign-state", "Saved campaign variables are malformed");
    for (const auto &variable : variables)
        if (!variable.is_primitive() || variable.is_null())
            fail("campaign-state", "Saved campaign variables require scalar values");
    const auto applied = graph.value("applied", Json::array());
    if (!applied.is_array() || applied.size() > 100000)
        fail("campaign-state", "Saved campaign attempt history is malformed");
    std::set<uint64_t> attempts;
    for (const auto &token : applied)
        if (!token.is_number_integer() || token < 1 || token >= graph["attempt"] ||
            !attempts.insert(token.get<uint64_t>()).second)
            fail("campaign-state", "Saved campaign attempt history is invalid");
}
void save_bundle(const fs::path &path, const Json &state, const std::vector<uint8_t> &world,
                 const ResolvedContentSet &resolved) {
    if (world.size() > kWorldLimit)
        fail("save-limit", "World snapshot exceeds 512 MiB");
    Json metadata = {{"schema_version", 1},
                     {"lock", resolved.lock},
                     {"gameplay_digest", resolved.gameplay_digest},
                     {"session_digest", resolved.session_digest},
                     {"state", state},
                     {"world_sha256", hash_world(world)}};
    std::string bytes = metadata.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        fail("save-limit", "Semantic save metadata exceeds 16 MiB");
    auto parent = path.parent_path().empty() ? fs::current_path() : path.parent_path();
    fs::create_directories(platform::host_path(parent));
    fs::path stage =
        parent /
        (".save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(platform::host_path(stage)))
        fail("save-busy", "Cannot reserve save staging directory");
    try {
        fs::path temporary = stage / "snapshot";
        {
            std::ofstream output(platform::host_path(temporary), std::ios::binary);
            output.write("CCSAVE1\n", 8);
            put64(output, bytes.size());
            put64(output, world.size());
            output << sha256(bytes) << bytes;
            output.write(reinterpret_cast<const char *>(world.data()),
                         static_cast<std::streamsize>(world.size()));
            if (!output.flush())
                fail("save-write", "Could not finish content save");
        }
        // Verify the complete staged pair before replacing the last good save.
        read_bundle(temporary);
        if (!platform::sync_file(temporary))
            fail("save-flush", "Could not sync the staged content save");
#if defined(_WIN32)
        if (!MoveFileExW(platform::host_path(temporary).c_str(), platform::host_path(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            fail("save-activate", "Could not atomically activate save");
#else
        fs::rename(platform::host_path(temporary), platform::host_path(path));
#endif
        fs::remove(platform::host_path(stage));
    } catch (...) {
        std::error_code error;
        fs::remove_all(platform::host_path(stage), error);
        throw;
    }
}
SavedState load_bundle(const fs::path &path, const ResolvedContentSet &resolved) {
    auto bundle = read_bundle(path);
    if (bundle.metadata.at("lock") != resolved.lock ||
        bundle.metadata.at("gameplay_digest") != resolved.gameplay_digest ||
        bundle.metadata.at("session_digest") != resolved.session_digest)
        fail("save-content", "Saved package lock or gameplay identity differs; restore its profile "
                             "or explicitly migrate a copy");
    return std::move(bundle.payload);
}
Json migrate_bundle(const fs::path &source, const fs::path &output, const Json &migration,
                    const ResolvedContentSet &resolved, bool preview) {
    if (fs::exists(platform::host_path(output)))
        fail("migration-output", "Migration needs a new output file");
    auto bundle = read_bundle(source);
    const Json previous_state = bundle.payload.state;
    const auto previous_world_bytes = bundle.payload.world.size();
    if (!migration.contains("schema_version") ||
        !migration.at("schema_version").is_number_integer() ||
        migration.at("schema_version") != 1 ||
        migration.at("source_lock_sha256") != sha256(bundle.metadata.at("lock").dump()) ||
        migration.at("target_lock_sha256") != sha256(resolved.lock.dump()))
        fail("migration-lock", "Migration does not bind these exact package locks");
    Json ids = migration.value("ids", Json::object());
    if (!ids.is_object())
        fail("migration-ids", "Expected stable ID mappings");
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (!valid_content_id(it.key()) || !it.value().is_string() || !valid_content_id(it.value()))
            fail("migration-ids", "Invalid identity mapping");
        bool available = false;
        for (const auto &definition : resolved.definitions)
            if (definition.id == it.value())
                available = true;
        const std::string target = it.value().get<std::string>();
        if (!available &&
            !(target.rfind("stock:", 0) == 0 && target.find('.') != std::string::npos))
            fail("migration-content", "Mapped target content is unavailable", {}, it.value());
    }
    migrate_state(bundle.payload.state, ids, resolved);
    if (migration.contains("campaign_nodes")) {
        auto &graph = bundle.payload.state.at("graph");
        const auto &nodes = migration.at("campaign_nodes");
        const auto *campaign =
            resolved.find("campaign", bundle.payload.state.at("campaign").get<std::string>());
        if (!nodes.is_object() || nodes.size() > 1024 || !campaign || campaign->behavior != "graph")
            fail("migration-nodes", "Campaign node mappings require a bounded target graph");
        for (auto node = nodes.begin(); node != nodes.end(); ++node) {
            if (node.key().empty() || !node.value().is_string() ||
                node.value().get_ref<const std::string &>().empty())
                fail("migration-nodes", "Campaign node mappings require nonempty strings");
            campaign_node(*campaign, node.value().get<std::string>());
        }
        auto old = graph.at("node").get<std::string>();
        if (nodes.contains(old))
            graph["node"] = nodes.at(old);
    }
    if (migration.contains("variables")) {
        auto &variables = bundle.payload.state.at("graph").at("variables");
        const auto &names = migration.at("variables");
        if (!variables.is_object() || !names.is_object())
            fail("migration-variables", "Campaign variable mappings require objects");
        for (auto name = names.begin(); name != names.end(); ++name)
            if (name.key().empty() || !name.value().is_string() ||
                name.value().get_ref<const std::string &>().empty())
                fail("migration-variables", "Campaign variable mappings require nonempty strings");
        Json renamed = Json::object();
        for (auto it = variables.begin(); it != variables.end(); ++it) {
            std::string name = migration.at("variables").value(it.key(), it.key());
            if (renamed.contains(name))
                fail("migration-collision", "Campaign variable mappings collide");
            renamed[name] = it.value();
        }
        variables = std::move(renamed);
    }
    validate_saved_campaign_graph(bundle.payload.state, resolved);
    if (migration.value("reset_world", false)) {
        if (!migration.value("mission_boundary", false))
            fail("migration-world",
                 "World reset is allowed only at an explicitly declared mission boundary");
        bundle.payload.world.clear();
        bundle.payload.state["world_snapshot"] = false;
        bundle.payload.state["snapshot_mission"] = "";
        bundle.payload.state["world_digest"] = migration.at("world_digest");
    } else {
        // Native snapshots contain opaque simulation state and session-local
        // numeric handles. Semantic ID mappings cannot rewrite those bytes.
        if (!bundle.payload.world.empty() &&
            (bundle.metadata.at("gameplay_digest") != resolved.gameplay_digest ||
             bundle.metadata.at("session_digest") != resolved.session_digest))
            fail("migration-world",
                 "Changing gameplay or session identities with a native snapshot requires an "
                 "explicit boundary reset");
        if (migration.contains("world_digest") &&
            bundle.payload.state.value("world_digest", std::string{}) !=
                migration.at("world_digest"))
            fail("migration-world",
                 "An incompatible world snapshot requires an explicit boundary reset");
    }
    if (migration.contains("base_digest")) {
        if (bundle.payload.state.value("base_digest", std::string{}) !=
                migration.at("base_digest") &&
            (!migration.value("reset_world", false) || !migration.value("mission_boundary", false)))
            fail("migration-world",
                 "Changing base gameplay resources requires an explicit boundary reset");
        bundle.payload.state["base_digest"] = migration.at("base_digest");
    }
    if (migration.contains("simulation_options")) {
        const auto &options = migration.at("simulation_options");
        if (!options.is_number_unsigned() &&
            (!options.is_number_integer() || options.get<int64_t>() < 0))
            fail("migration-world", "Simulation options must be a nonnegative integer");
        if (options.get<uint64_t>() > UINT32_MAX)
            fail("migration-world", "Simulation options exceed the supported protocol");
        if (bundle.payload.state.value("simulation_options", Json()) != options &&
            (!migration.value("reset_world", false) || !migration.value("mission_boundary", false)))
            fail("migration-world",
                 "Changing simulation options requires an explicit boundary reset");
        bundle.payload.state["simulation_options"] = options;
    }
    fs::path backup = output;
    backup += ".source-backup";
    if (fs::exists(platform::host_path(backup)))
        fail("migration-output", "Migration backup already exists");
    Json report = {{"preview", preview},
                   {"source_sha256", file_sha256(source)},
                   {"state_changes", Json::diff(previous_state, bundle.payload.state)},
                   {"world_bytes_before", previous_world_bytes},
                   {"world_bytes_after", bundle.payload.world.size()},
                   {"target_lock_sha256", sha256(resolved.lock.dump())},
                   {"backup", fs::absolute(backup).generic_u8string()}};
    if (preview)
        return report;
    fs::copy_file(platform::host_path(source), platform::host_path(backup));
    if (file_sha256(source) != file_sha256(backup))
        fail("migration-backup", "Could not verify source backup");
    save_bundle(output, bundle.payload.state, bundle.payload.world, resolved);
    return report;
}
} // namespace cc::content
