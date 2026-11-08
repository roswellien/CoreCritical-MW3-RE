#include "check.h"
#include "content/content.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zsave/zsave.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/content_world_state.h"
#include "platform/host_path.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(platform::host_path(cc::content::fs::u8path(path)), std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = in.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<uint8_t> build_zar_archive(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string data;
    std::vector<zutl_zar::Entry> table;
    for (const auto& [name, body] : files) {
        zutl_zar::Entry e{};
        e.start = static_cast<uint32_t>(data.size());
        e.length = static_cast<uint32_t>(body.size());
        std::snprintf(e.name, sizeof e.name, "%s", name.c_str());
        table.push_back(e);
        data += body;
    }
    const uint32_t count = static_cast<uint32_t>(table.size());
    data.append(reinterpret_cast<const char*>(table.data()), table.size() * sizeof(zutl_zar::Entry));
    const uint32_t footer[2] = {1, count};
    data.append(reinterpret_cast<const char*>(footer), 8);
    return std::vector<uint8_t>(data.begin(), data.end());
}

struct StateReset {
    ~StateReset() {
        mech3::clear_content_world_activation();
        zdec::craters().history.clear();
        zsave::shutdown();
        zcls::classes_reset();
    }
};
} // namespace

TEST(content_world_activation_survives_save_and_restore_with_exact_ids) {
    StateReset guard;
    zcls::classes_reset();
    zsave::init();

    auto* root = zcls::object3d_create();
    auto* child1 = zcls::object3d_create();
    auto* child2 = zcls::object3d_create();
    zcls::node_set_name(root, "root_inst");
    zcls::node_set_name(child1, "node_child1");
    zcls::node_set_name(child2, "node_child2");
    zcls::object3d_add_child(root, child1);
    zcls::object3d_add_child(root, child2);
    zcls::update_node(root);

    EXPECT((root->flags() & 4) != 0);
    EXPECT((child1->flags() & 4) != 0);
    EXPECT((child2->flags() & 4) != 0);

    // Disable child2
    zcls::node_set_active(child2, 0);
    EXPECT((child2->flags() & 4) == 0);
    EXPECT((child1->flags() & 4) != 0);

    std::map<std::string, zcls::Node*> nodes = {
        {"root:main", root},
        {"node:part1", child1},
        {"node:part2", child2}
    };
    mech3::bind_content_world_activation("world-digest-1234", nodes);

    const std::string save_path = std::string(MW3_TEST_TMP) + "/test_act_survive.zbd";
    EXPECT(zsave::save(save_path.c_str(), 2));

    // Genuine teardown of native nodes and activation bindings
    mech3::clear_content_world_activation();
    zcls::classes_reset();

    // Recreate equivalent authored scene (new node pointers, different allocation IDs)
    auto* new_root = zcls::object3d_create();
    auto* new_child1 = zcls::object3d_create();
    auto* new_child2 = zcls::object3d_create();
    zcls::object3d_add_child(new_root, new_child1);
    zcls::object3d_add_child(new_root, new_child2);
    zcls::update_node(new_root);

    // By default all newly created nodes are active
    EXPECT((new_child2->flags() & 4) != 0);

    std::map<std::string, zcls::Node*> new_nodes = {
        {"root:main", new_root},
        {"node:part1", new_child1},
        {"node:part2", new_child2}
    };
    mech3::bind_content_world_activation("world-digest-1234", new_nodes);

    // Restore via native zsave::load
    EXPECT(zsave::load(save_path.c_str(), 2));

    // Verify changed activation state was restored to equivalent new node with same stable ID
    EXPECT((new_child2->flags() & 4) == 0);
    EXPECT((new_child1->flags() & 4) != 0);
    EXPECT((new_root->flags() & 4) != 0);
}

TEST(content_world_activation_registration_order_and_cleanup_lifetimes) {
    StateReset guard;
    zcls::classes_reset();

    // 1. World binding precedes zsave manager initialization
    zsave::shutdown();
    mech3::clear_content_world_activation();

    auto* node = zcls::object3d_create();
    zcls::node_set_name(node, "early_node");
    mech3::bind_content_world_activation("world-digest-order", {{"root:test", node}});

    // Manager is null, so content_world_activation_registered is false
    EXPECT(!mech3::content_world_activation_registered());

    // Now initialize zsave and re-register activation (as GameEngine::save_init does)
    zsave::init();
    mech3::register_content_world_activation();

    EXPECT(mech3::content_world_activation_registered());

    // Disable node and save
    zcls::node_set_active(node, 0);
    const std::string save_path = std::string(MW3_TEST_TMP) + "/test_order_save.zbd";
    EXPECT(zsave::save(save_path.c_str(), 2));

    // Reset node and load
    zcls::node_set_active(node, 1);
    EXPECT(zsave::load(save_path.c_str(), 2));
    EXPECT((node->flags() & 4) == 0);

    // 2. Clear activation unregisters registrant and leaves stock path unaffected
    mech3::clear_content_world_activation();
    EXPECT(!mech3::content_world_activation_registered());

    const std::string stock_path = std::string(MW3_TEST_TMP) + "/test_stock_save.zbd";
    EXPECT(zsave::save(stock_path.c_str(), 2));

    auto bytes = read_file_bytes(stock_path);
    EXPECT(!bytes.empty());
    // validate_content_world_snapshot must return without error when g_digest is empty
    mech3::validate_content_world_snapshot(bytes, false);
    mech3::validate_content_world_snapshot(bytes, true);
}

TEST(content_world_activation_invalid_rebinding_preserves_the_accepted_scene) {
    StateReset guard;
    zcls::classes_reset();
    zsave::init();
    auto* accepted = zcls::object3d_create();
    auto* replacement = zcls::object3d_create();
    mech3::bind_content_world_activation("accepted", {{"node:accepted", accepted}});
    zcls::node_set_active(accepted, 0);
    const std::string path = std::string(MW3_TEST_TMP) + "/activation-rebinding.zbd";
    EXPECT(zsave::save(path.c_str(), 2));
    bool rejected = false;
    try {
        mech3::bind_content_world_activation(
            "replacement", {{"node:a", replacement}, {"node:z", nullptr}});
    } catch (const cc::content::Error& error) {
        rejected = error.diagnostic.code == "save-world-state";
    }
    EXPECT(rejected);
    zcls::node_set_active(accepted, 1);
    bool restored = false;
    try {
        restored = zsave::load(path.c_str(), 2);
    } catch (const cc::content::Error&) {
        // Report broken binding preservation as a failed check, not a test-run abort.
    }
    EXPECT(restored);
    EXPECT((accepted->flags() & 4) == 0);
}

TEST(content_world_activation_preflight_rejects_corrupt_records_and_preserves_scene) {
    StateReset guard;
    zcls::classes_reset();
    zsave::init();

    auto* root = zcls::object3d_create();
    auto* child = zcls::object3d_create();
    zcls::object3d_add_child(root, child);
    zcls::update_node(root);

    mech3::bind_content_world_activation("digest-valid", {{"root:r", root}, {"node:c", child}});
    mech3::register_content_world_activation();

    const std::string valid_path = std::string(MW3_TEST_TMP) + "/test_preflight_valid.zbd";
    EXPECT(zsave::save(valid_path.c_str(), 2));
    auto valid_bytes = read_file_bytes(valid_path);
    EXPECT(!valid_bytes.empty());

    // Valid bytes pass preflight
    mech3::validate_content_world_snapshot(valid_bytes, false);
    mech3::validate_content_world_snapshot(valid_bytes, true);

    // Truncated bytes fail
    bool threw_truncated = false;
    try {
        std::vector<uint8_t> trunc = {1, 2, 3, 4};
        mech3::validate_content_world_snapshot(trunc, true);
    } catch (const cc::content::Error& e) {
        threw_truncated = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_truncated);

    // Header kind
    const char header_data[8] = {8, 0, 0, 0, 2, 0, 0, 0};
    const std::string header_str(header_data, 8);

    // Wrong world digest
    bool threw_wrong_digest = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"wrong\",\"deformed\":false,\"nodes\":{\"node:c\":true,\"root:r\":true}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_wrong_digest = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_wrong_digest);

    // Missing node ID
    bool threw_missing_id = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"digest-valid\",\"deformed\":false,\"nodes\":{\"root:r\":true}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_missing_id = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_missing_id);

    // Extra unknown node ID
    bool threw_extra_id = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"digest-valid\",\"deformed\":false,\"nodes\":{\"node:c\":true,\"root:r\":true,\"node:unknown\":true}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_extra_id = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_extra_id);

    // Non-boolean node value
    bool threw_non_boolean = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"digest-valid\",\"deformed\":false,\"nodes\":{\"node:c\":true,\"root:r\":\"invalid\"}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_non_boolean = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_non_boolean);

    // Duplicate archive entry
    bool threw_duplicate_entry = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"digest-valid\",\"deformed\":false,\"nodes\":{\"node:c\":true,\"root:r\":true}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_duplicate_entry = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_duplicate_entry);

    // Deformed history rejected during restoration
    bool threw_deformed_restore = false;
    try {
        std::string json = "{\"format\":\"ContentWorldActivation\",\"schema\":1,\"world_digest\":\"digest-valid\",\"deformed\":true,\"nodes\":{\"node:c\":true,\"root:r\":true}}";
        auto archive = build_zar_archive({{"zSaveHeader", header_str}, {"ContentWorld/Activation", json}});
        mech3::validate_content_world_snapshot(archive, true);
    } catch (const cc::content::Error& e) {
        threw_deformed_restore = (e.diagnostic.code == "save-world-deformation");
    }
    EXPECT(threw_deformed_restore);

    // Scene flags remained completely untouched throughout all preflight failures
    EXPECT((root->flags() & 4) != 0);
    EXPECT((child->flags() & 4) != 0);
}

TEST(content_world_activation_deformation_fails_visibly_and_subsequent_save_succeeds) {
    StateReset guard;
    zcls::classes_reset();
    zsave::init();

    auto* root = zcls::object3d_create();
    mech3::bind_content_world_activation("digest-deform-test", {{"root:r", root}});
    mech3::register_content_world_activation();

    const std::string save_first = std::string(MW3_TEST_TMP) + "/test_deform_first.zbd";
    EXPECT(zsave::save(save_first.c_str(), 2));
    const auto first_bytes = read_file_bytes(save_first);
    EXPECT(!first_bytes.empty());

    // Inject crater history (simulating deformation)
    zdec::HistoryEntry crater{};
    zdec::craters().history.push_back(crater);

    const std::string save_fail = std::string(MW3_TEST_TMP) + "/test_deform_fail.zbd";
    bool threw_deform_save = false;
    try {
        (void)zsave::save(save_fail.c_str(), 2);
    } catch (const cc::content::Error& e) {
        threw_deform_save = (e.diagnostic.code == "save-world-deformation");
    }
    EXPECT(threw_deform_save);

    // The first save bundle remains intact and valid
    const auto verify_first = read_file_bytes(save_first);
    EXPECT(verify_first == first_bytes);

    // Clear deformation
    zdec::craters().history.clear();

    // Subsequent save succeeds cleanly
    const std::string save_subsequent = std::string(MW3_TEST_TMP) + "/test_deform_subsequent.zbd";
    EXPECT(zsave::save(save_subsequent.c_str(), 2));
    const auto subsequent_bytes = read_file_bytes(save_subsequent);
    EXPECT(!subsequent_bytes.empty());
    mech3::validate_content_world_snapshot(subsequent_bytes, false);
    mech3::validate_content_world_snapshot(subsequent_bytes, true);
}

TEST(content_world_activation_detects_deleted_or_reused_node_slots) {
    StateReset guard;
    zcls::classes_reset();
    zsave::init();

    auto* node1 = zcls::object3d_create();
    zcls::node_set_name(node1, "node1");

    mech3::bind_content_world_activation("digest-slot-test", {{"node:test", node1}});
    mech3::register_content_world_activation();

    // 1. Destroy node1 directly
    zcls::node_destroy(node1);

    const std::string save_fail1 = std::string(MW3_TEST_TMP) + "/test_slot_fail1.zbd";
    bool threw_deleted = false;
    try {
        (void)zsave::save(save_fail1.c_str(), 2);
    } catch (const cc::content::Error& e) {
        threw_deleted = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_deleted);

    // 2. Allocate a new node into the recycled slot: allocation_id must differ
    auto* node2 = zcls::object3d_create();
    zcls::node_set_name(node2, "node2");

    bool threw_reused = false;
    try {
        (void)zsave::save(save_fail1.c_str(), 2);
    } catch (const cc::content::Error& e) {
        threw_reused = (e.diagnostic.code == "save-world-state");
    }
    EXPECT(threw_reused);
}
