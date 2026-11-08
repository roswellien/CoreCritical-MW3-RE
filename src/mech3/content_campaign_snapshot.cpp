#include "mech3/content_campaign.h"
#include "mech3/content_world_state.h"
#include "mech3/content_state.h"
#include "platform/host_path.h"

#include <chrono>
#include <cstring>
#include <fstream>

#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/pilot.h"

namespace mech3 {
using cc::content::fail;
using cc::content::Json;

Json campaign_native_state(const CampaignGame &game) {
    Json lance = Json::array();
    for (const auto *slot : game.lancemates)
        lance.push_back(slot_state(*slot));
    return {{"store", store_state(game.store)},
            {"salvage", store_state(game.store3c)},
            {"loadouts", store_state(game.store50)},
            {"mission_store", store_state(game.store64)},
            {"extra_store", store_state(game.store78)},
            {"player", slot_state(game.slot)},
            {"lance", lance},
            {"score", game.c4},
            {"kills", {game.b4[0], game.b4[1], game.b4[2], game.b4[3]}},
            {"a8", game.a8},
            {"ac", game.ac},
            {"x9c", game.x9c}};
}
void restore_campaign_native_state(CampaignGame &game, const Json &state) {
    // Resolve every saved identity before committing any part of the pilot. A
    // missing item/skin therefore leaves the loaded game's semantic state intact.
    CampaignGame staged(game.pilot, game.campaign, game.mission);
    restore_store(staged.store, state.at("store"));
    restore_store(staged.store3c, state.at("salvage"));
    restore_store(staged.store50, state.at("loadouts"));
    restore_store(staged.store64, state.at("mission_store"));
    restore_store(staged.store78, state.at("extra_store"));
    restore_slot(staged.slot, state.at("player"));
    const auto &lance = state.at("lance");
    if (!lance.is_array() || lance.size() > 3)
        fail("save-lance", "Saved lance exceeds supported native slots");
    for (const auto &slot : lance) {
        auto next = std::make_unique<GameSlot>(-1, nullptr, 0);
        restore_slot(*next, slot);
        staged.lancemates.push_back(next.release());
    }
    const auto kills = state.at("kills");
    if (kills.size() != 4)
        fail("save-score", "Saved campaign kill record is invalid");
    staged.c4 = state.at("score").get<int32_t>();
    for (size_t i = 0; i < 4; ++i)
        staged.b4[i] = kills[i].get<int32_t>();
    staged.a8 = state.at("a8").get<int32_t>();
    staged.ac = state.at("ac").get<int32_t>();
    staged.x9c = state.at("x9c").get<int32_t>();
    game.store.assign(staged.store);
    game.store3c.assign(staged.store3c);
    game.store50.assign(staged.store50);
    game.store64.assign(staged.store64);
    game.store78.assign(staged.store78);
    game.slot.copy_from(staged.slot);
    game.reset_lancemates(true);
    game.copy_lancemates(staged);
    game.c4 = staged.c4;
    for (size_t i = 0; i < 4; ++i)
        game.b4[i] = staged.b4[i];
    game.a8 = staged.a8;
    game.ac = staged.ac;
    game.x9c = staged.x9c;
}

std::vector<uint8_t> capture_content_world_snapshot(const std::string &path) {
    static uint64_t serial = 0;
    std::string staged;
    do {
        // Keep the temporary basename short: a profile and run already consume
        // most of the native Windows path budget. Do not append to the run name.
        staged = (cc::content::fs::path(path).parent_path() /
                  (".snapshot-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                   "-" + std::to_string(++serial)))
                     .generic_string();
    } while (cc::content::fs::exists(
        platform::host_path(cc::content::fs::u8path(zutl::write_path(staged.c_str())))));
    const auto host = zutl::write_path(staged.c_str());
    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::error_code ec;
            cc::content::fs::remove(platform::host_path(cc::content::fs::u8path(path)), ec);
        }
    } cleanup{host};
    if (!pilot_hooks().save_world_checked(staged.c_str(), 2))
        fail("save-native", "Native world snapshot failed; the previous bundle was preserved");
    std::ifstream in(platform::host_path(cc::content::fs::u8path(host)),
                     std::ios::binary | std::ios::ate);
    const auto length = in ? in.tellg() : std::streampos(-1);
    if (length < 8 || length > std::streampos(512 * 1024 * 1024))
        fail("save-native", "Native world snapshot is missing or exceeds its size limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size())))
        fail("save-native", "Native world snapshot could not be read completely");
    uint32_t tail[2];
    std::memcpy(tail, bytes.data() + bytes.size() - 8, 8);
    if (tail[0] != 1 || tail[1] == 0 ||
        uint64_t(tail[1]) * sizeof(zutl_zar::Entry) > bytes.size() - 8)
        fail("save-native", "Native world snapshot has an incomplete archive table");
    const auto table = bytes.size() - 8 - size_t(tail[1]) * sizeof(zutl_zar::Entry);
    int headers = 0;
    for (uint32_t i = 0; i < tail[1]; ++i) {
        zutl_zar::Entry entry;
        std::memcpy(&entry, bytes.data() + table + i * sizeof entry, sizeof entry);
        if (!std::memchr(entry.name, 0, sizeof entry.name) ||
            uint64_t(entry.start) + entry.length > table)
            fail("save-native", "Native world snapshot contains an incomplete archive entry");
        if (std::strcmp(entry.name, "zSaveHeader") == 0) {
            uint32_t header[2]{};
            if (entry.length == 8)
                std::memcpy(header, bytes.data() + entry.start, 8);
            if (header[0] != 8 || header[1] != 2)
                fail("save-native", "Native world snapshot has an incompatible header");
            ++headers;
        }
    }
    if (headers != 1)
        fail("save-native", "Native world snapshot lacks its unique header");
    validate_content_world_snapshot(bytes, false);
    return bytes;
}
} // namespace mech3
