#include "mech3/content_playtest.h"

#include <memory>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "mech3/app_engine.h"
#include "mech3/app_mech.h"
#include "mech3/content_network.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_world.h"
#include "mech3/mech_config.h"
#include "mech3/mech_reader.h"
#include "mech3/pilot.h"

namespace mech3 {
namespace {
using cc::content::Definition;
using cc::content::fail;
using cc::content::Json;
const Json *selection(const cc::content::ResolvedContentSet &content) {
    if (!content.lock.contains("selections") ||
        !content.lock.at("selections").contains("multiplayer"))
        return nullptr;
    return &content.lock.at("selections").at("multiplayer");
}
const Definition &arena(const std::string &id) {
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", id) : nullptr;
    if (!mission || !mission->data.value("multiplayer", false) || mission->handle < 0 ||
        mission->handle >= 0x40000000)
        fail("multiplayer-mission", "Direct multiplayer playtest requires a registered arena", {},
             id);
    return *mission;
}
uint32_t arena_tag(const std::string &mission) {
    return 0x40000000u | static_cast<uint32_t>(arena(mission).handle);
}
ContentPeerCheck refused(std::string *reason, const std::string &message) {
    if (reason)
        *reason = message;
    return ContentPeerCheck::incompatible;
}
} // namespace

bool content_mission_is_multiplayer(const std::string &id) {
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", id) : nullptr;
    return mission && mission->data.value("multiplayer", false);
}

void validate_content_multiplayer_selection(const cc::content::ResolvedContentSet &content) {
    const Json *choice = selection(content);
    if (!choice)
        return;
    if (!choice->is_object() || choice->size() != 3)
        fail("multiplayer-selection", "Multiplayer selection requires chassis, loadout and skin");
    for (const char *key : {"chassis", "loadout", "skin"})
        if (!choice->contains(key) || !choice->at(key).is_string() ||
            choice->at(key).get_ref<const std::string &>().empty() ||
            choice->at(key).get_ref<const std::string &>().size() >
                (std::string(key) == "loadout" ? 80u : 200u))
            fail("multiplayer-selection",
                 "Multiplayer selection requires bounded stable identities", {}, {}, key);
    const auto *chassis = content.find("chassis", choice->at("chassis").get<std::string>());
    if (!chassis)
        fail("multiplayer-chassis", "Selected multiplayer chassis is unavailable");
    const auto skin_id = choice->at("skin").get<std::string>();
    const auto *skin = content.find("skin", skin_id);
    if (skin && skin->data.at("chassis") != chassis->id)
        fail("multiplayer-skin", "Selected skin belongs to another chassis");
    if (!skin && skin_id.rfind("stock:", 0) != 0)
        fail("multiplayer-skin", "Selected multiplayer skin is unavailable");
}

bool configure_content_multiplayer_launch(GameEngine &engine, Pilot &pilot,
                                          const std::string &mission_id) {
    const auto content = active_content();
    const auto &mission = arena(mission_id);
    if (!znet::active() || !managed_gameplay_session())
        fail("multiplayer-network",
             "Arena playtest requires an active managed host or join session");
    validate_content_multiplayer_selection(*content);
    MultiplayerGame *game = pilot.multi;
    if (!game)
        fail("multiplayer-pilot", "The selected pilot has no multiplayer game");
    if (const Json *choice = selection(*content)) {
        const auto *chassis = content->find("chassis", choice->at("chassis").get<std::string>());
        const MechDfn *dfn = mech_dfn(chassis->handle);
        const std::string loadout = choice->at("loadout").get<std::string>();
        const Config *config = dfn && dfn->configs ? dfn->configs->find(loadout.c_str()) : nullptr;
        if (!config)
            fail("multiplayer-loadout", "Selected native multiplayer loadout is unavailable", {},
                 chassis->id, loadout);
        const int skin = content_skin_index(chassis->handle, choice->at("skin").get<std::string>());
        if (skin < 0)
            fail("multiplayer-skin",
                 "Selected native multiplayer skin is unavailable for this chassis");
        game->slot.mech = chassis->handle;
        if (!game->slot.set_config(config))
            fail("multiplayer-loadout", "Cannot copy selected native loadout");
        game->slot.skin = skin;
    }
    game->set_world(static_cast<int32_t>(arena_tag(mission_id)));
    game->content_multiplayer_mission = mission_id;
    game->x70 = 1 << 30; // Direct soak/playtest has no early score limit.
    pilot.current = game;
    GameEngine::Launch launch;
    launch.type = 3;
    launch.multi = game;
    launch.keep_pilot = true;
    launch.auto_launch = true;
    launch.mech = game->slot.mech;
    launch.skin = game->slot.skin;
    launch.op = mission.data.value("operation", 0);
    if (!select_content_multiplayer(game->world(), launch.index, launch.number))
        fail("multiplayer-mission", "Cannot select the authored arena");
    program_net_debug_lobby();
    game->clear_lobby();
    int start = 1;
    for (const auto &peer : znet::lists().players) {
        auto row = std::make_unique<LobbyPlayer>();
        row->id = peer->id;
        row->order = start++;
        row->state = 3;
        game->lobby_players.push_back(std::move(row));
    }
    net_set_lobby_left([](uint32_t id) {
        auto *pilot = current_pilot();
        auto *game = pilot ? pilot->multiplayer() : nullptr;
        auto *row = game ? game->lobby_player(id) : nullptr;
        if (!row)
            fail("multiplayer-player", "Departing arena peer has no native roster record");
        row->left = 1;
    });
    engine.set_launch(launch);
    engine.launch_slot_refresh();
    return true;
}

std::vector<uint32_t> content_playtest_identity(const std::string &mission) {
    if (!managed_gameplay_session())
        fail("multiplayer-content", "Arena requires managed gameplay content");
    // Kind/skin/ready/world count/world tag match the production lobby record.
    std::vector<uint32_t> words{1, 1, 3, 1, arena_tag(mission)};
    append_content_session(words);
    return words;
}

ContentPeerCheck check_content_playtest_identity(const std::vector<uint32_t> &words,
                                                 const std::string &mission, std::string *reason) {
    if (words.empty())
        return ContentPeerCheck::pending;
    if (words[0] == 1) {
        if (!content_sessions_match(words, reason))
            return ContentPeerCheck::incompatible;
        if (words.size() < 5 || words[3] != 1 || words[4] != arena_tag(mission))
            return refused(reason, "Peers selected different arena missions");
        return ContentPeerCheck::compatible;
    }
    // A compatible peer may reach native mech construction while this peer is
    // still polling. Its kind-2 record replaces lobby data, but retains the full
    // validated managed identity and loadout. Validate it rather than timing out.
    if (words[0] == 2 && words.size() >= 7) {
        try {
            std::string skin, peer_mission;
            std::unique_ptr<Config> config(
                unpack_managed_config(words.data() + 3, words.size() - 3, &skin, &peer_mission));
            if (!config || config->mech != static_cast<int32_t>(words[2]) || skin.empty())
                return refused(reason,
                               "Peer native loadout does not match its chassis and skin record");
            if (peer_mission != mission)
                return refused(reason, "Peer native loadout belongs to a different arena mission");
            return ContentPeerCheck::compatible;
        } catch (const std::exception &error) {
            return refused(reason, error.what());
        }
    }
    return refused(reason, "Peer has no supported managed lobby or native loadout identity");
}

void publish_content_playtest_identity(const std::string &mission) {
    const auto words = content_playtest_identity(mission);
    if (!znet::active() || !znet::set_player_data(znet::local_id(), words.data(),
                                                  static_cast<uint32_t>(words.size() * 4)))
        fail("multiplayer-preflight", "Cannot publish the local arena content identity");
}

ContentPeerCheck content_playtest_peers(const std::string &mission, size_t required,
                                        std::string *reason) {
    if (!znet::active())
        return refused(reason, "The network session ended during arena preflight");
    bool pending = znet::lists().players.size() < required;
    for (const auto &peer : znet::lists().players) {
        if (peer->id == znet::local_id())
            continue;
        const uint32_t bytes = znet::player_data_size(peer->id);
        if (!bytes) {
            pending = true;
            continue;
        }
        if (bytes % 4 || bytes > 65544)
            return refused(reason, "Peer identity record has an invalid size");
        std::vector<uint32_t> words(bytes / 4);
        uint32_t received = bytes;
        if (!znet::get_player_data(peer->id, words.data(), &received) || received != bytes) {
            pending = true;
            continue;
        }
        const auto checked = check_content_playtest_identity(words, mission, reason);
        if (checked == ContentPeerCheck::incompatible)
            return checked;
        if (checked == ContentPeerCheck::pending)
            pending = true;
    }
    return pending ? ContentPeerCheck::pending : ContentPeerCheck::compatible;
}
} // namespace mech3
