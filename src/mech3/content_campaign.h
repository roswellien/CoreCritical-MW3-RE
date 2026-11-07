#pragma once

#include "content/content.h"

namespace mech3 {
class CampaignGame;
class GameEngine;
class Pilot;

void append_campaign_stock_content(std::vector<cc::content::Definition> &definitions);
cc::content::Capabilities campaign_capabilities();
void validate_campaign_content(const cc::content::ResolvedContentSet &content);
cc::content::Json begin_content_campaign(const cc::content::Definition &campaign);
// Pure bounded reducer. Results bind their stable run identity and attempt;
// committing the same result twice leaves state and rewards unchanged.
cc::content::Json reduce_content_campaign(const cc::content::Definition &campaign,
                                          const cc::content::Json &state,
                                          const cc::content::Json &result);
bool configure_content_launch(GameEngine &engine, const std::string &mission,
                              const std::string &campaign = {});
Pilot *obtain_content_pilot();
void configure_existing_content_launch(GameEngine &engine, Pilot &pilot, CampaignGame &game);
int32_t content_campaign_operation_alias(const Pilot &pilot, const std::string &id);
// Stages launch initialization, restores selection/launch globals, and returns
// the target world identity. The caller retains the initialized staged game.
std::string preflight_content_campaign_launch(CampaignGame &game);
// Bind a newly selected graph's run before producing its managed save path.
void bind_content_campaign_run(CampaignGame &game);
bool content_campaign_launch(CampaignGame &game, int32_t &world, int32_t &number,
                             int32_t &operation);
CampaignGame *advance_content_campaign(CampaignGame &game, bool next, bool save);
void save_content_campaign(CampaignGame &game);
// Persist pilot/loadout edits while retaining the bundle's accepted world.
void refresh_content_campaign_semantics(CampaignGame &game);
// Captures only a new native snapshot. Never reads a prior save at `path`.
std::vector<uint8_t> capture_content_world_snapshot(const std::string &path);
void read_content_campaign(CampaignGame &game);
// Validates environment, expected run and all semantic identities before apply.
// Importers set content_run (and stock operation/mission) before this call.
void apply_content_campaign_state(CampaignGame &game, const cc::content::Json &state);
bool restore_content_campaign(CampaignGame &game);
bool content_campaign_terminal();
void append_content_campaign_choices(Pilot &pilot);
CampaignGame &select_content_standalone(Pilot &pilot, const std::string &mission);
std::string content_campaign_label(const CampaignGame &game);
bool consume_content_initial_launch();

} // namespace mech3
