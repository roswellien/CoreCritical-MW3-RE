#include "check.h"
#include "content/hash.h"
#include "mech3/content_playtest.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_world.h"
#include "mech3/mech_config.h"

#include <utility>

namespace {
using cc::content::Json;
using cc::content::ResolvedContentSet;
struct Session {
    std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
    ~Session() { mech3::activate_content(previous); }
};
std::shared_ptr<ResolvedContentSet> arena_content() {
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    for (int i = 0; i < 2; ++i)
        content->definitions.push_back(
            {"mission",
             "demo:arena" + std::to_string(i),
             "demo",
             "native",
             {{"world", "stock:c1"}, {"number", 1}, {"multiplayer", true}},
             1000 + i});
    content->gameplay_digest = cc::content::sha256("arena gameplay");
    content->session_digest = cc::content::sha256("arena mapping");
    return content;
}
} // namespace

TEST(content_playtest_preflight_rejects_mismatched_arena_gameplay_and_truncation) {
    Session session;
    mech3::activate_content(arena_content());
    using Status = mech3::ContentPeerCheck;
    const auto valid = mech3::content_playtest_identity("demo:arena0");
    EXPECT(mech3::check_content_playtest_identity({}, "demo:arena0") == Status::pending);
    EXPECT(mech3::check_content_playtest_identity(valid, "demo:arena0") == Status::compatible);
    EXPECT(mech3::check_content_playtest_identity(valid, "demo:arena1") == Status::incompatible);
    auto changed = valid;
    changed[9] ^= 1; // Gameplay digest in the existing content envelope.
    EXPECT(mech3::check_content_playtest_identity(changed, "demo:arena0") == Status::incompatible);
    changed = valid;
    changed.back() ^= 1; // Native simulation options.
    EXPECT(mech3::check_content_playtest_identity(changed, "demo:arena0") == Status::incompatible);
    changed.pop_back();
    EXPECT(mech3::check_content_playtest_identity(changed, "demo:arena0") == Status::incompatible);
}

TEST(content_playtest_accepts_only_complete_matching_native_peer_loadout_after_preflight) {
    Session session;
    mech3::activate_content(arena_content());
    using Status = mech3::ContentPeerCheck;
    mech3::Config config(0, "network");
    config.armor_tech = 3;
    config.sink_tech = 3;
    EXPECT(mech3::start_content_mission("demo:arena0"));
    auto packed = mech3::pack_managed_config(config, "cosmetic:blue");
    std::vector<uint32_t> native{2, 0, 0};
    native.insert(native.end(), packed.begin(), packed.end());
    EXPECT(mech3::check_content_playtest_identity(native, "demo:arena0") == Status::compatible);
    // Same package/session digest, different arena: the kind-2 race fallback
    // must retain the mission check after the peer replaces its lobby record.
    EXPECT(mech3::check_content_playtest_identity(native, "demo:arena1") == Status::incompatible);
    EXPECT(mech3::start_content_mission("demo:arena1"));
    const auto other_packed = mech3::pack_managed_config(config, "cosmetic:blue");
    std::vector<uint32_t> other{2, 0, 0};
    other.insert(other.end(), other_packed.begin(), other_packed.end());
    EXPECT(mech3::start_content_mission("demo:arena0"));
    EXPECT(mech3::check_content_playtest_identity(other, "demo:arena0") == Status::incompatible);
    native[2] = 1;
    EXPECT(mech3::check_content_playtest_identity(native, "demo:arena0") == Status::incompatible);
    native[2] = 0;
    native.pop_back();
    EXPECT(mech3::check_content_playtest_identity(native, "demo:arena0") == Status::incompatible);
}

TEST(content_playtest_selection_rejects_missing_unknown_and_incompatible_fields) {
    auto content = arena_content();
    content->lock["selections"]["multiplayer"] = {{"chassis", "stock:bushwacker"},
                                                  {"loadout", "Native"},
                                                  {"skin", "stock:bushwacker.default"}};
    mech3::validate_content_multiplayer_selection(*content);
    for (const auto &entry : {std::pair<const char *, Json>{"chassis", "absent:chassis"},
                              {"loadout", false},
                              {"skin", "absent:skin"},
                              {"script", "run"}}) {
        auto changed = *content;
        changed.lock["selections"]["multiplayer"][entry.first] = entry.second;
        bool rejected = false;
        try {
            mech3::validate_content_multiplayer_selection(changed);
        } catch (const cc::content::Error &) {
            rejected = true;
        }
        EXPECT(rejected);
    }
}
