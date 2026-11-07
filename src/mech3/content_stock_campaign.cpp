#include "mech3/content_stock_campaign.h"

#include "content/persistence.h"

#include "mech3/content_campaign.h"
#include "mech3/content_registry.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"

namespace mech3 {
namespace {
using cc::content::fail;
using cc::content::Json;
std::string mission_id(const CampaignGame &game) {
    return "stock:c" + std::to_string(game.operation()) + "m" +
           std::to_string(game.mission_number());
}
void select_mission(CampaignGame &game) {
    const auto content = active_content();
    const auto id = mission_id(game);
    if (!content || !content->find("mission", id))
        fail("campaign-mission", "Stock campaign mission is unavailable", {}, id);
    game.content_mission = id;
    game.content_state["stock_operation"] = game.operation();
    game.content_state["stock_mission"] = game.mission_number();
}
} // namespace

bool content_stock_campaign(const CampaignGame &game) {
    return game.content_campaign == "stock:campaign";
}
bool adopt_content_stock_campaign(CampaignGame &game) {
    if (content_stock_campaign(game)) {
        bind_content_campaign_run(game);
        return true;
    }
    if (!active_content() || !game.content_mission.empty() || game.operation() < 1 ||
        game.operation() > 4)
        return false;
    game.content_campaign = "stock:campaign";
    game.content_state = {{"schema", 1},
                          {"campaign", "stock:campaign"},
                          {"run", cc::content::new_campaign_run_id(game.content_campaign)},
                          {"terminal", false}};
    select_mission(game);
    bind_content_campaign_run(game);
    return true;
}
void carry_content_stock_campaign(const CampaignGame &from, CampaignGame &to) {
    to.content_campaign = from.content_campaign;
    to.content_run = from.content_run;
    to.content_state = from.content_state;
    to.content_state["terminal"] = false;
    select_mission(to);
    bind_content_campaign_run(to);
}
void finish_content_stock_campaign(CampaignGame &game) {
    if (game.content_state.value("terminal", false))
        return;
    game.content_state["terminal"] = true;
    if (missions().loaded)
        save_content_campaign(game);
}
void validate_content_stock_campaign(const CampaignGame &game) {
    const auto &state = game.content_state;
    const auto run = state.value("run", std::string{});
    if (!state.contains("schema") || !state["schema"].is_number_integer() || state["schema"] != 1 ||
        state.value("campaign", std::string{}) != "stock:campaign" ||
        !cc::content::valid_campaign_run_id(run) || !state.contains("stock_operation") ||
        !state["stock_operation"].is_number_integer() || !state.contains("stock_mission") ||
        !state["stock_mission"].is_number_integer() ||
        state.value("stock_operation", Json()) != game.operation() ||
        state.value("stock_mission", Json()) != game.mission_number() ||
        game.content_mission != mission_id(game) || !state.contains("terminal") ||
        !state["terminal"].is_boolean())
        fail("campaign-state", "Saved stock campaign progression or run identity is invalid");
}
} // namespace mech3
