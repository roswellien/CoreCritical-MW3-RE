#include "mech3/content_network.h"
#include "gamez/zutil/zfix.h"
#include "mech3/content_base.h"
#include "mech3/content_registry.h"

#include <algorithm>
#include <array>

namespace mech3 {
namespace {
using cc::content::Json;
constexpr uint32_t kMarker = 0x31564343, kMods = 0x53444f4d;
bool cosmetic(const std::string &kind) {
    return kind == "skin" || kind == "hud" || kind == "appearance" || kind == "audio" ||
           kind == "texture" || kind == "legacy_layer";
}
Json semantics(const std::vector<cc::content::Definition> &definitions) {
    std::vector<const cc::content::Definition *> ordered;
    for (const auto &definition : definitions)
        if (!cosmetic(definition.kind))
            ordered.push_back(&definition);
    std::sort(ordered.begin(), ordered.end(), [](auto *a, auto *b) {
        return std::make_pair(a->kind, a->id) < std::make_pair(b->kind, b->id);
    });
    Json result = Json::array();
    for (auto *value : ordered)
        result.push_back({value->kind, value->id, value->behavior, value->data});
    return result;
}
std::array<uint32_t, 8> digest_words(const std::string &digest) {
    if (digest.size() != 64)
        cc::content::fail("network-digest", "Invalid content digest");
    std::array<uint32_t, 8> result{};
    for (size_t i = 0; i < 32; ++i) {
        auto nibble = [](char c) -> uint32_t {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            cc::content::fail("network-digest", "Invalid content digest encoding");
        };
        result[i / 4] |= ((nibble(digest[2 * i]) << 4) | nibble(digest[2 * i + 1]))
                         << ((i % 4) * 8);
    }
    return result;
}
} // namespace
bool managed_gameplay_session() {
    auto content = active_content();
    if (!content)
        return false;
    static std::weak_ptr<const cc::content::ResolvedContentSet> previous;
    static bool changed = false;
    if (previous.lock() != content) {
        changed = semantics(content->definitions).dump() != semantics(stock_content()).dump();
        for (const auto &claim : content->lock.value("legacy_resources", Json::array()))
            if (claim.at("gameplay").get<bool>())
                changed = true;
        previous = content;
    }
    return changed;
}
uint32_t content_simulation_options() {
    const auto &options = zfix::options();
    return uint32_t(options.per_second) | (uint32_t(options.bugs) << 1) |
           (uint32_t(options.fpu53) << 2) | (uint32_t(options.nav_allies) << 3) |
           (uint32_t(options.nav_enemies) << 4);
}
void append_content_session(std::vector<uint32_t> &player) {
    if (!managed_gameplay_session())
        return;
    auto content = active_content();
    player.insert(player.end(), {kMarker, 1, 29, 1});
    auto gameplay = digest_words(content->gameplay_digest),
         mapping = digest_words(content->session_digest);
    player.insert(player.end(), gameplay.begin(), gameplay.end());
    player.insert(player.end(), mapping.begin(), mapping.end());
    auto base = digest_words(content_base_digest());
    player.insert(player.end(), base.begin(), base.end());
    player.push_back(content_simulation_options());
}
bool content_sessions_match(const std::vector<uint32_t> &player, std::string *reason) {
    auto reject = [&](const char *message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (player.size() < 4 || player[0] != 1)
        return !managed_gameplay_session() || reject("Peer has no compatible content handshake");
    if (player[3] > 65536)
        return reject("Peer world-list count is invalid");
    size_t at = std::max<size_t>(5, 4 + player[3]);
    if (at > player.size())
        return reject("Peer lobby record is truncated");
    if (at < player.size() && player[at] == kMods) {
        if (player.size() - at < 4 || player[at + 3] > 16)
            return reject("Peer mod record is malformed");
        uint32_t count = player[at + 3];
        at += 4;
        for (uint32_t i = 0; i < count; ++i) {
            if (at >= player.size() || player[at] > 64)
                return reject("Peer mod label is malformed");
            size_t words = (player[at++] + 3) / 4;
            if (words > player.size() - at)
                return reject("Peer mod label is truncated");
            at += words;
        }
    }
    bool present = at < player.size() && player[at] == kMarker;
    if (!present)
        return !managed_gameplay_session() ||
               reject("Peer does not support the required content protocol");
    if (player.size() - at != 29 || player[at + 1] != 1 || player[at + 2] != 29 ||
        player[at + 3] != 1)
        return reject("Peer content protocol is unsupported or malformed");
    if (!managed_gameplay_session())
        return reject("Peer requires a different gameplay content profile");
    auto content = active_content();
    auto gameplay = digest_words(content->gameplay_digest),
         mapping = digest_words(content->session_digest);
    if (!std::equal(gameplay.begin(), gameplay.end(), player.begin() + at + 4) ||
        !std::equal(mapping.begin(), mapping.end(), player.begin() + at + 12))
        return reject("Resolved gameplay definitions or session identities differ");
    auto base = digest_words(content_base_digest());
    if (!std::equal(base.begin(), base.end(), player.begin() + at + 20) ||
        player[at + 28] != content_simulation_options())
        return reject("Base gameplay resources or simulation options differ");
    return true;
}
} // namespace mech3
