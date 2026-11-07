#pragma once
#include "content/content.h"
#include <cstdio>
#include <memory>

namespace mech3 {
struct Config;
struct ConfigItem;
struct GameSlot;
class Store;
class Pilot;
class CampaignGame;
// Atomically replaces the pilot index after its complete native records flush.
bool save_managed_pilot(Pilot &pilot);
// Managed-only identity extension follows the native 12-byte pilot header and
// must be checked before any game records are read.
bool write_managed_pilot_identity(std::FILE *output);
void read_managed_pilot_identity(std::FILE *input);
cc::content::Json item_state(const ConfigItem &item);
std::unique_ptr<ConfigItem> item_from_state(const cc::content::Json &state);
cc::content::Json config_state(const Config &config);
std::unique_ptr<Config> config_from_state(const cc::content::Json &state);
void restore_config(Config &target, const cc::content::Json &state);
cc::content::Json store_state(const Store &store);
void restore_store(Store &store, const cc::content::Json &state);
cc::content::Json slot_state(const GameSlot &slot);
void restore_slot(GameSlot &slot, const cc::content::Json &state);
uint32_t save_managed_store(std::FILE *output, const Store &store);
void load_managed_store(std::FILE *input, Store &store);
void save_managed_slot(std::FILE *output, const GameSlot &slot);
void load_managed_slot(std::FILE *input, GameSlot &slot);
// False means an ordinary legacy record. Recognized malformed records throw.
bool load_managed_config(std::FILE *input, Config &target);
uint32_t save_managed_config(std::FILE *output, const Config &config);
std::vector<uint32_t> pack_managed_config(const Config &config, const std::string &skin = {});
Config *unpack_managed_config(const uint32_t *words, size_t count, std::string *skin = nullptr,
                              std::string *mission = nullptr);
constexpr uint32_t content_loadout_marker = 0xfffe0000u;
cc::content::Json campaign_native_state(const CampaignGame &game);
void restore_campaign_native_state(CampaignGame &game, const cc::content::Json &state);
} // namespace mech3
