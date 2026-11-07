#include "mech3/content_campaign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <set>

#include "content/persistence.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/app_engine.h"
#include "mech3/content_base.h"
#include "mech3/content_campaign_items.h"
#include "mech3/content_network.h"
#include "mech3/content_playtest.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/content_stock_campaign.h"
#include "mech3/content_world.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_reader.h"
#include "mech3/mission.h"
#include "mech3/pilot.h"
#include "mech3/zai_valve.h"

namespace mech3 {
Objectives &mission_objectives();
namespace {
using cc::content::Definition;
using cc::content::fail;
using cc::content::Json;
using cc::content::ResolvedContentSet;
bool g_terminal = false;
bool g_initial_launch = false;
uint64_t g_running_attempt = 0;
std::string g_running_run;

const Json &node_for(const Definition &campaign, const std::string &id) {
    for (const auto &node : campaign.data.at("nodes"))
        if (node.value("id", "") == id)
            return node;
    fail("campaign-node", "Campaign references an unknown node", campaign.owner, campaign.id, id);
}

bool condition(const Json &rule, const Json &state, const Json &result, int depth = 0) {
    if (depth > 32)
        fail("campaign-rule-depth", "Campaign rule nesting exceeds 32 levels");
    if (rule.is_boolean())
        return rule.get<bool>();
    if (!rule.is_object() || rule.empty())
        fail("campaign-rule", "A rule must be a condition object");
    if (rule.contains("all") || rule.contains("any")) {
        const bool all = rule.contains("all");
        const auto &values = rule[all ? "all" : "any"];
        if (!values.is_array() || values.size() > 128)
            fail("campaign-rule", "Rule groups require a bounded array");
        bool answer = all;
        for (const auto &v : values) {
            const bool hit = condition(v, state, result, depth + 1);
            answer = all ? (answer && hit) : (answer || hit);
        }
        return answer;
    }
    if (rule.contains("not"))
        return !condition(rule["not"], state, result, depth + 1);
    Json actual;
    if (rule.contains("outcome")) {
        if (!rule["outcome"].is_string() ||
            (rule["outcome"] != "success" && rule["outcome"] != "failure"))
            fail("campaign-rule", "Outcome condition must name success or failure");
        return result.value("outcome", "") == rule["outcome"].get<std::string>();
    }
    if (rule.contains("objective"))
        actual = result.value("objectives", Json::object())
                     .value(rule["objective"].get<std::string>(), Json(false));
    else if (rule.contains("variable"))
        actual = state.value("variables", Json::object())
                     .value(rule["variable"].get<std::string>(), Json());
    else if (rule.contains("stat"))
        actual =
            result.value("stats", Json::object()).value(rule["stat"].get<std::string>(), Json());
    else if (rule.contains("valve"))
        actual =
            result.value("valves", Json::object()).value(rule["valve"].get<std::string>(), Json());
    else if (rule.contains("item"))
        actual = state.value("mission_items", Json::object())
                     .value(rule["item"].get<std::string>(), Json(0));
    else
        fail("campaign-rule", "Unknown required campaign condition");
    if (rule.contains("equals"))
        return actual == rule["equals"];
    if (rule.contains("gte")) {
        if (!actual.is_number() || !rule["gte"].is_number())
            return false;
        return actual.get<double>() >= rule["gte"].get<double>();
    }
    if (rule.contains("lte")) {
        if (!actual.is_number() || !rule["lte"].is_number())
            return false;
        return actual.get<double>() <= rule["lte"].get<double>();
    }
    fail("campaign-rule", "Condition needs equals, gte or lte");
}

void validate_condition(const Json &rule, const std::set<std::string> &variables,
                        const Definition &mission, const ResolvedContentSet &content,
                        int depth = 0) {
    if (depth > 32)
        fail("campaign-rule-depth", "Campaign rule nesting exceeds 32 levels");
    if (rule.is_boolean())
        return;
    if (!rule.is_object() || rule.empty())
        fail("campaign-rule", "Campaign condition must be a nonempty object");
    if (rule.contains("all") || rule.contains("any") || rule.contains("not")) {
        if (rule.size() != 1)
            fail("campaign-rule", "Condition groups accept exactly one operator");
        if (rule.contains("not")) {
            validate_condition(rule["not"], variables, mission, content, depth + 1);
            return;
        }
        const auto &group = rule[rule.contains("all") ? "all" : "any"];
        if (!group.is_array() || group.size() > 128)
            fail("campaign-rule", "Condition group must be a bounded array");
        for (const auto &child : group)
            validate_condition(child, variables, mission, content, depth + 1);
        return;
    }
    if (rule.contains("outcome")) {
        if (rule.size() != 1 || (rule["outcome"] != "success" && rule["outcome"] != "failure"))
            fail("campaign-rule", "Outcome condition must name success or failure");
        return;
    }
    int sources = 0, comparisons = 0;
    for (const char *key : {"objective", "variable", "stat", "valve", "item"})
        if (rule.contains(key)) {
            ++sources;
            if (!rule[key].is_string())
                fail("campaign-rule", "Condition reference must be a stable string identity");
            const auto name = rule[key].get<std::string>();
            const std::string kind = key;
            if (kind == "objective" &&
                !mission.data.value("objective_ids", Json::object()).contains(name))
                fail("campaign-rule",
                     "Condition references an objective not exported by this mission: " + name);
            if (kind == "variable" && !variables.count(name))
                fail("campaign-rule",
                     "Condition references an undeclared campaign variable: " + name);
            if (kind == "stat" && name != "score" && name != "time")
                fail("campaign-rule",
                     "Condition references an unsupported mission statistic: " + name);
            if (kind == "item" && !content.find("mission_item", name))
                fail("campaign-rule", "Condition references an unavailable mission item: " + name);
            if (kind == "valve") {
                const auto valves = mission.data.value("export_valves", Json::array());
                if (std::find(valves.begin(), valves.end(), Json(name)) == valves.end())
                    fail("campaign-rule",
                         "Condition references a valve not exported by this mission: " + name);
            }
        }
    for (const char *key : {"equals", "gte", "lte"})
        if (rule.contains(key)) {
            ++comparisons;
            if (rule[key].is_null() || !rule[key].is_primitive() ||
                (std::string(key) != "equals" && !rule[key].is_number()))
                fail("campaign-rule", "Condition comparison requires a compatible scalar");
        }
    if (sources != 1 || comparisons != 1 || rule.size() != 2)
        fail("campaign-rule", "Condition requires one source and one supported comparison");
}

void validate_actions(const Json &actions, const Definition &campaign,
                      const ResolvedContentSet &content) {
    if (!actions.is_array() || actions.size() > 128)
        fail("campaign-actions", "Campaign actions must be a bounded array");
    for (const auto &action : actions) {
        const std::string op = action.value("op", "");
        if (op == "set" || op == "add") {
            if (action.value("variable", "").empty() || !action.contains("value") ||
                !action["value"].is_primitive() || action["value"].is_null() ||
                (op == "add" && !action["value"].is_number()))
                fail("campaign-action", "Variable action needs a name and scalar value");
        } else if (op == "give" || op == "take") {
            campaign_items::validate(action, campaign, content);
        } else
            fail("campaign-action", "Unknown required campaign action: " + op, campaign.owner,
                 campaign.id);
    }
}
void run_actions(Json &state, const Json &actions, Json &rewards) {
    for (const auto &action : actions) {
        const std::string op = action.at("op").get<std::string>();
        if (op == "set")
            state["variables"][action.at("variable").get<std::string>()] = action.at("value");
        else if (op == "add") {
            const auto name = action.at("variable").get<std::string>();
            const auto previous = state["variables"].value(name, Json(0));
            if (!previous.is_number())
                fail("campaign-variable", "Cannot increment a nonnumeric campaign variable");
            const double total = previous.get<double>() + action.at("value").get<double>();
            if (!std::isfinite(total))
                fail("campaign-variable", "Campaign variable arithmetic overflowed");
            state["variables"][name] = total;
        } else if (action.value("kind", std::string{}) == "mission_item") {
            const auto id = action.at("id").get<std::string>();
            const int count = action.value("count", 1) * (op == "take" ? -1 : 1);
            const int previous = state["mission_items"].value(id, 0);
            if (count < 0 && previous < -count)
                fail("campaign-item",
                     "Cannot consume a mission item the campaign does not hold: " + id);
            const int64_t total = int64_t(previous) + count;
            if (total > INT32_MAX)
                fail("campaign-item", "Mission item count exceeds its supported range");
            state["mission_items"][id] = total;
        } else
            rewards.push_back(action);
    }
}

Json capture_result(const CampaignGame &game, bool success) {
    const auto content = active_content();
    const auto *mission = content->find("mission", game.content_mission);
    Json result = {{"run", g_running_run},
                   {"attempt", g_running_attempt},
                   {"outcome", success ? "success" : "failure"},
                   {"objectives", Json::object()},
                   {"valves", Json::object()},
                   {"stats", {{"score", game.c4}, {"time", missions().time}}}};
    const Objectives &all = mission_objectives();
    const Json ids = mission->data.value("objective_ids", Json::object());
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        const auto &path = it.value();
        const std::string group = path.at(0).get<std::string>();
        const Objective *current = group == "primary"     ? all.primary.get()
                                   : group == "secondary" ? all.secondary.get()
                                   : group == "final"     ? all.final.get()
                                   : group == "fail"      ? all.fail.get()
                                                          : nullptr;
        for (size_t k = 1; k < path.size() && current; ++k) {
            const auto *list = dynamic_cast<const ObjectiveGroup *>(current);
            const auto index = path[k].get<size_t>();
            if (!list || index >= list->items.size())
                fail("campaign-objective", "Declared objective path does not exist: " + it.key());
            auto item = list->items.begin();
            std::advance(item, static_cast<std::ptrdiff_t>(index));
            current = item->get();
        }
        if (!current)
            fail("campaign-objective", "Declared objective group is unavailable: " + it.key());
        result["objectives"][it.key()] = current->done != 0;
    }
    for (const auto &v : mission->data.value("export_valves", Json::array())) {
        const auto name = v.get<std::string>();
        const auto *valve = ai::valve_find(name.c_str());
        if (!valve)
            fail("campaign-valve", "Declared campaign valve does not exist: " + name);
        result["valves"][name] = valve->get();
    }
    return result;
}

std::string bundle_path(CampaignGame &game) {
    return zutl::write_path((std::string(game.path()) + ".ccsave").c_str());
}

int32_t operation_alias(const Pilot &pilot, const std::string &id) {
    uint32_t hash = 0;
    const auto digest = cc::content::sha256(id);
    for (size_t k = 0; k < 7; ++k)
        hash = hash * 16 + static_cast<uint32_t>(std::stoi(digest.substr(k, 1), nullptr, 16));
    int32_t value = 1000 + static_cast<int32_t>(hash);
    while (std::any_of(pilot.campaigns.begin(), pilot.campaigns.end(), [&](const Game *entry) {
        const auto *game = dynamic_cast<const CampaignGame *>(entry);
        return game && game->campaign == value;
    }))
        ++value;
    return value;
}

void prepare_campaign(CampaignGame &game, const Definition &campaign,
                      const ResolvedContentSet &content) {
    if (game.content_state.value("initialized", false))
        return;
    campaign_items::apply(game, game.content_state.value("rewards", Json::array()));
    game.content_state["rewards"] = Json::array();
    if (campaign.data.contains("starting_chassis")) {
        const auto *chassis =
            content.find("chassis", campaign.data["starting_chassis"].get<std::string>());
        if (!chassis)
            fail("campaign-chassis", "Starting chassis is unavailable");
        const auto *dfn = mech_dfn(chassis->handle);
        if (!dfn || !dfn->configs)
            fail("campaign-loadout", "Starting chassis has no native loadouts");
        const auto requested = campaign.data.value("starting_loadout", std::string{});
        const Config *chosen = nullptr;
        if (!requested.empty())
            chosen = dfn->configs->find(requested.c_str());
        else if (!dfn->configs->configs.empty())
            chosen = dfn->configs->configs.front();
        if (!chosen)
            fail("campaign-loadout", "Starting loadout is unavailable");
        game.slot.mech = chassis->handle;
        delete game.slot.config;
        game.slot.config = pilot_hooks().config_copy(*chosen);
        game.store.add_mech(chassis->handle);
        if (campaign.data.contains("starting_skin")) {
            const int skin = content_skin_index(chassis->handle,
                                                campaign.data["starting_skin"].get<std::string>());
            if (skin < 0)
                fail("campaign-skin", "Starting skin is unavailable for the selected chassis");
            game.slot.skin = skin;
        }
    }
    game.content_state["initialized"] = true;
}

void validate_save_environment(const Json &state) {
    if (state.value("base_digest", std::string{}) != content_base_digest())
        fail("save-base",
             "Native gameplay resources differ from this checkpoint; use explicit migration");
    if (!state.contains("simulation_options") || !state["simulation_options"].is_number_integer() ||
        state["simulation_options"] != content_simulation_options())
        fail("save-options", "Simulation options differ from this checkpoint; restore the original "
                             "options or migrate explicitly");
}
} // namespace

Pilot *obtain_content_pilot() {
    if (auto *selected = current_pilot())
        return selected;
    if (auto *loaded = find_pilot("CoreCritical"))
        return loaded;
    const auto path = zutl::write_path("pilots/CoreCritical.mw3");
    if (cc::content::fs::exists(path)) {
        if (auto *loaded = load_pilot("CoreCritical", false))
            return loaded;
        fail("campaign-pilot",
             "Existing managed pilot could not be read; original file was preserved");
    }
    return create_pilot("CoreCritical");
}
int32_t content_campaign_operation_alias(const Pilot &pilot, const std::string &id) {
    return operation_alias(pilot, id);
}
void bind_content_campaign_run(CampaignGame &game) {
    if (game.content_mission.empty())
        return;
    auto run = game.content_state.value("run", std::string{});
    if (run.empty() && game.content_campaign.empty() && game.content_run.empty()) {
        run = cc::content::new_campaign_run_id(game.content_mission);
        game.content_state["run"] = run;
    }
    if (!cc::content::valid_campaign_run_id(run) ||
        (!game.content_run.empty() && game.content_run != run))
        fail("campaign-run", "Campaign graph does not match its expected run identity");
    game.content_run = std::move(run);
}

void append_campaign_stock_content(std::vector<Definition> &definitions) {
    Json nodes = Json::array();
    for (int op = 1; op <= 4; ++op)
        for (int m = 1; m <= (op < 3 ? 4 : 6); ++m) {
            const auto id = "c" + std::to_string(op) + "m" + std::to_string(m);
            nodes.push_back({{"id", id}, {"mission", "stock:" + id}});
        }
    definitions.push_back({"campaign",
                           "stock:campaign",
                           "stock",
                           "legacy",
                           {{"name", "MechWarrior 3"}, {"nodes", nodes}},
                           0});
}
cc::content::Capabilities campaign_capabilities() {
    return {{"campaign.graph", 1}, {"campaign.legacy", 1}, {"mission_item.token-v1", 1}};
}
void validate_campaign_content(const ResolvedContentSet &content) {
    for (const auto *item : content.of_kind("mission_item")) {
        if (item->behavior != "token-v1" || item->data.value("name", std::string{}).empty())
            fail("mission-item", "Mission items require the supported token behavior and a name",
                 item->owner, item->id);
    }
    for (const auto *campaign : content.of_kind("campaign")) {
        if (campaign->behavior == "legacy" && campaign->owner == "stock")
            continue;
        if (campaign->behavior != "graph")
            fail("campaign-behavior", "Unknown required campaign behavior", campaign->owner,
                 campaign->id);
        const auto nodes = campaign->data.value("nodes", Json::array());
        if (!nodes.is_array() || nodes.empty() || nodes.size() > 1024)
            fail("campaign-nodes", "Campaign requires one to 1024 mission nodes");
        std::set<std::string> ids, variables;
        const auto declared = campaign->data.value("variables", Json::object());
        if (!declared.is_object())
            fail("campaign-variable", "Campaign variables must be an object");
        for (auto it = declared.begin(); it != declared.end(); ++it) {
            if (it.key().empty() || !it.value().is_primitive() || it.value().is_null())
                fail("campaign-variable",
                     "Campaign variable declarations require names and scalar values");
            variables.insert(it.key());
        }
        auto note_variables = [&](const Json &actions) {
            for (const auto &action : actions)
                if (action.value("op", std::string{}) == "set" ||
                    action.value("op", std::string{}) == "add")
                    variables.insert(action.value("variable", std::string{}));
        };
        note_variables(campaign->data.value("starting_inventory", Json::array()));
        for (const auto &node : nodes) {
            note_variables(node.value("on_success", Json::array()));
            note_variables(node.value("on_failure", Json::array()));
            for (const auto &reward : node.value("objective_rewards", Json::object()))
                note_variables(reward);
        }
        for (const auto &node : nodes) {
            const auto id = node.value("id", std::string{});
            if (id.empty() || !ids.insert(id).second)
                fail("campaign-node", "Duplicate or empty campaign node");
            if (!content.find("mission", node.value("mission", std::string{})))
                fail("campaign-mission", "Campaign node references missing mission",
                     campaign->owner, campaign->id, id);
            for (const char *key : {"on_success", "on_failure"})
                validate_actions(node.value(key, Json::array()), *campaign, content);
            const Json objective_rewards = node.value("objective_rewards", Json::object());
            if (!objective_rewards.is_object())
                fail("campaign-rewards",
                     "Objective rewards must be keyed by stable objective identity");
            const auto *mission = content.find("mission", node.at("mission").get<std::string>());
            for (auto reward = objective_rewards.begin(); reward != objective_rewards.end();
                 ++reward) {
                if (!mission->data.value("objective_ids", Json::object()).contains(reward.key()))
                    fail("campaign-objective",
                         "Reward references an objective not exported by its mission");
                validate_actions(reward.value(), *campaign, content);
            }
        }
        if (!ids.count(campaign->data.value("start", std::string{})))
            fail("campaign-start", "Campaign has no valid start node");
        for (const auto &node : nodes) {
            const auto routes = node.value("routes", Json::array());
            if (!routes.is_array() || routes.size() > 128)
                fail("campaign-routes", "Campaign routes must be a bounded ordered array");
            for (const auto &route : routes) {
                if (!route.contains("next") ||
                    (!route["next"].is_null() && !ids.count(route["next"].get<std::string>())))
                    fail("campaign-route", "Campaign route targets a missing node");
                const auto *mission =
                    content.find("mission", node.at("mission").get<std::string>());
                validate_condition(route.value("when", Json(true)), variables, *mission, content);
            }
        }
        validate_actions(campaign->data.value("starting_inventory", Json::array()), *campaign,
                         content);
        if (campaign->data.contains("starting_chassis") &&
            !content.find("chassis", campaign->data["starting_chassis"].get<std::string>()))
            fail("campaign-chassis", "Campaign starting chassis is missing");
    }
}
Json begin_content_campaign(const Definition &campaign) {
    Json state = {{"schema", 1},
                  {"campaign", campaign.id},
                  {"run", cc::content::new_campaign_run_id(campaign.id)},
                  {"node", campaign.data.at("start")},
                  {"variables", campaign.data.value("variables", Json::object())},
                  {"mission_items", Json::object()},
                  {"attempt", uint64_t(1)},
                  {"applied", Json::array()},
                  {"terminal", false}};
    Json rewards = Json::array();
    run_actions(state, campaign.data.value("starting_inventory", Json::array()), rewards);
    state["rewards"] = rewards;
    return state;
}
Json reduce_content_campaign(const Definition &campaign, const Json &previous, const Json &result) {
    if (!previous.contains("schema") || !previous["schema"].is_number_integer() ||
        previous["schema"] != 1)
        fail("campaign-schema", "Saved campaign graph uses an unsupported state version");
    if (previous.value("campaign", std::string{}) != campaign.id)
        fail("campaign-state", "Result does not belong to this campaign");
    const auto run = previous.value("run", std::string{});
    if (!cc::content::valid_campaign_run_id(run) || !result.contains("run") ||
        !result["run"].is_string() || result["run"] != run)
        fail("campaign-run", "Mission result does not belong to this campaign run");
    const uint64_t attempt = result.at("attempt").get<uint64_t>();
    const Json applied = previous.value("applied", Json::array());
    if (std::find(applied.begin(), applied.end(), Json(attempt)) != applied.end())
        return previous;
    if (previous.value("terminal", false))
        fail("campaign-state", "Campaign has already ended");
    if (applied.size() >= 100000 || attempt == UINT64_MAX)
        fail("campaign-state", "Campaign progression exceeds its supported limit");
    if (attempt != previous.at("attempt").get<uint64_t>())
        fail("campaign-attempt", "Stale or future mission result");
    Json state = previous, rewards = Json::array();
    const auto &node = node_for(campaign, state.at("node").get<std::string>());
    const auto outcome = result.value("outcome", std::string{});
    if (outcome != "success" && outcome != "failure")
        fail("campaign-result", "Unknown mission result outcome");
    run_actions(state,
                node.value(outcome == "success" ? "on_success" : "on_failure", Json::array()),
                rewards);
    const Json objective_rewards = node.value("objective_rewards", Json::object());
    for (auto reward = objective_rewards.begin(); reward != objective_rewards.end(); ++reward)
        if (result.value("objectives", Json::object()).value(reward.key(), false))
            run_actions(state, reward.value(), rewards);
    bool routed = false;
    for (const auto &route : node.value("routes", Json::array()))
        if (condition(route.value("when", Json(true)), state, result)) {
            state["terminal"] = route.at("next").is_null();
            if (!state["terminal"].get<bool>())
                state["node"] = route["next"];
            routed = true;
            break;
        }
    if (!routed && outcome == "success")
        state["terminal"] = true;
    state["applied"].push_back(attempt);
    state["attempt"] = attempt + 1;
    state["last_result"] = result;
    state["rewards"] = rewards;
    return state;
}

bool content_campaign_launch(CampaignGame &game, int32_t &world, int32_t &number,
                             int32_t &operation) {
    adopt_content_stock_campaign(game);
    if (game.content_mission.empty())
        return false;
    bind_content_campaign_run(game);
    const auto content = active_content();
    const auto *mission = content ? content->find("mission", game.content_mission) : nullptr;
    if (content && !game.content_campaign.empty() && !content_stock_campaign(game)) {
        const auto *campaign = content->find("campaign", game.content_campaign);
        if (!campaign)
            fail("campaign-missing", "Saved campaign content is unavailable");
        prepare_campaign(game, *campaign, *content);
    }
    if (!mission)
        fail("mission-missing", "Saved mission content is not available: " + game.content_mission);
    game.content_placeholder = false;
    const auto *definition = content->find("world", mission->data.at("world").get<std::string>());
    world = content_world_index(*definition);
    if (world < 0)
        fail("world-missing", "Selected world is not registered");
    number = mission->data.value("number", 1);
    operation = mission->data.value("operation", 0);
    g_running_attempt = game.content_state.value("attempt", uint64_t{});
    g_running_run = game.content_state.value("run", std::string{});
    start_content_mission(mission->id);
    return true;
}

std::string preflight_content_campaign_launch(CampaignGame &game) {
    const auto selected = content_selected_mission();
    const auto running_run = g_running_run;
    const auto running_attempt = g_running_attempt;
    const auto terminal = g_terminal, initial = g_initial_launch;
    std::string world_digest;
    std::exception_ptr error;
    try {
        int32_t world = -1, number = -1, operation = -1;
        if (!content_campaign_launch(game, world, number, operation))
            fail("campaign-launch", "Managed campaign preflight has no selected mission");
        world_digest = content_mission_world_digest(game.content_mission);
    } catch (...) {
        error = std::current_exception();
    }
    g_running_run = running_run;
    g_running_attempt = running_attempt;
    g_terminal = terminal;
    g_initial_launch = initial;
    if (selected.empty())
        clear_content_mission_selection();
    else if (!start_content_mission(selected))
        fail("campaign-launch", "Cannot restore the previous mission selection after preflight");
    if (error)
        std::rethrow_exception(error);
    return world_digest;
}

bool configure_content_launch(GameEngine &engine, const std::string &requested_mission,
                              const std::string &campaign_id) {
    const auto content = active_content();
    if (!content)
        return false;
    std::string mission_id = requested_mission;
    CampaignGame *game = nullptr;
    Pilot *pilot = obtain_content_pilot();
    if (!pilot)
        fail("campaign-pilot", "Cannot create the selected pilot");
    current_pilot() = pilot;
    engine.pilot_chosen(pilot);
    if (campaign_id.empty() && content_mission_is_multiplayer(mission_id)) {
        g_terminal = false;
        g_initial_launch = true;
        return configure_content_multiplayer_launch(engine, *pilot, mission_id);
    }
    append_content_campaign_choices(*pilot);
    if (!campaign_id.empty()) {
        const auto *campaign = content->find("campaign", campaign_id);
        if (!campaign || campaign->behavior != "graph")
            return false;
        for (auto it = pilot->campaigns.rbegin(); it != pilot->campaigns.rend(); ++it)
            if (auto *c = dynamic_cast<CampaignGame *>(*it);
                c && !c->content_placeholder && c->content_campaign == campaign_id &&
                !c->content_state.value("terminal", false)) {
                game = c;
                break;
            }
        if (!game)
            for (auto *existing : pilot->campaigns)
                if (auto *c = dynamic_cast<CampaignGame *>(existing);
                    c && c->content_placeholder && c->content_campaign == campaign_id) {
                    game = c;
                    break;
                }
        if (!game) {
            game = new CampaignGame(pilot, operation_alias(*pilot, campaign_id), 1);
            game->content_campaign = campaign_id;
            game->content_state = begin_content_campaign(*campaign);
            pilot->campaigns.push_back(game);
        }
        const auto &node = node_for(*campaign, game->content_state.at("node").get<std::string>());
        mission_id = node.at("mission").get<std::string>();
    } else {
        if (!content->find("mission", mission_id))
            return false;
        game = new CampaignGame(pilot, operation_alias(*pilot, mission_id), 1);
        pilot->campaigns.push_back(game);
    }
    game->content_mission = mission_id;
    configure_existing_content_launch(engine, *pilot, *game);
    return true;
}
void configure_existing_content_launch(GameEngine &engine, Pilot &pilot, CampaignGame &game) {
    current_pilot() = &pilot;
    engine.pilot_chosen(&pilot);
    pilot.current = &game;
    GameEngine::Launch launch;
    launch.keep_pilot = true;
    launch.auto_launch = true;
    launch.mech = game.slot.mech;
    launch.type = 1;
    content_campaign_launch(game, launch.index, launch.number, launch.op);
    launch.mech = game.slot.mech;
    engine.set_launch(launch);
    engine.launch_slot_refresh();
    g_terminal = false;
    g_initial_launch = true;
}

CampaignGame *advance_content_campaign(CampaignGame &game, bool next, bool save) {
    if (!missions().ended ||
        (!game.content_campaign.empty() &&
         (game.content_state.value("attempt", uint64_t{}) != g_running_attempt ||
          game.content_state.value("run", std::string{}) != g_running_run)))
        return &game;
    if (game.content_campaign.empty()) {
        if (next) {
            save_content_campaign(game);
            game.content_placeholder = false;
        }
        g_terminal = next;
        return next ? nullptr : &game;
    }
    const auto content = active_content();
    const auto *campaign = content->find("campaign", game.content_campaign);
    if (!campaign)
        fail("campaign-missing", "Selected campaign is unavailable at the result boundary");
    Json state = reduce_content_campaign(*campaign, game.content_state, capture_result(game, next));
    if (state.value("attempt", uint64_t{}) == game.content_state.value("attempt", uint64_t{}))
        return &game;
    auto following = std::make_unique<CampaignGame>(game.pilot, game.campaign, game.mission + 1);
    following->content_campaign = game.content_campaign;
    following->content_run = game.content_run;
    following->content_state = state;
    restore_campaign_native_state(*following, campaign_native_state(game));
    // Native mission results return the player's recovered loadout into store64.
    // Carry it into the next mission once, then reset the transient stock stores.
    following->store.add_all(game.store64);
    following->store50.clear();
    following->store64.clear();
    following->store78.clear();
    following->store3c.clear();
    if (campaign->data.value("salvage", true))
        if (auto *salvage = pilot_hooks().mission_salvage())
            following->store3c.assign(*salvage);
    campaign_items::apply(*following, state.at("rewards"));
    following->content_state["rewards"] = Json::array();
    if (state.value("terminal", false)) {
        following->mission = game.mission;
        following->content_mission = game.content_mission;
        save_content_campaign(*following);
        restore_campaign_native_state(game, campaign_native_state(*following));
        game.content_state = following->content_state;
        game.content_placeholder = false;
        g_terminal = true;
        return nullptr;
    }
    following->content_mission =
        node_for(*campaign, state.at("node").get<std::string>()).at("mission").get<std::string>();
    if (save)
        save_content_campaign(*following);
    following->b0 = 1;
    g_terminal = false;
    return following.release();
}
void save_content_campaign(CampaignGame &game) {
    if (game.content_mission.empty())
        return;
    bind_content_campaign_run(game);
    if (content_world_digest().empty() || !missions().loaded)
        fail("save-native", "A managed snapshot requires a loaded native mission");
    const auto content = active_content();
    if (!content)
        fail("campaign-content", "Managed campaign has no active content set");
    const auto world = capture_content_world_snapshot(game.path());
    const auto *source = content->find("mission", content_selected_mission());
    if (!source)
        fail("save-native", "Native snapshot has no selected source mission");
    Json state = {{"campaign", game.content_campaign},
                  {"mission", game.content_mission},
                  {"graph", game.content_state},
                  {"native", campaign_native_state(game)},
                  {"world_digest", content_world_digest()},
                  {"world_snapshot", true},
                  {"base_digest", content_base_digest()},
                  {"simulation_options", content_simulation_options()},
                  {"snapshot_mission", content_selected_mission()},
                  {"persistence_group", source->data.value("persistence_group", std::string{})}};
    cc::content::save_bundle(bundle_path(game), state, world, *content);
    game.content_checkpoint_expected = true;
}
void refresh_content_campaign_semantics(CampaignGame &game) {
    if (game.content_mission.empty() || game.content_placeholder)
        return;
    bind_content_campaign_run(game);
    const auto path = bundle_path(game);
    const auto content = active_content();
    if (!content)
        fail("campaign-content", "Managed campaign has no active content set");
    if (!cc::content::fs::exists(path)) {
        if (game.content_checkpoint_expected)
            fail("save-bundle", "The campaign's accepted checkpoint is missing");
        // A first briefing can save its starting inventory before any mission has
        // completed. This is an explicit semantic checkpoint, never stale world data.
        Json state = {{"campaign", game.content_campaign},
                      {"mission", game.content_mission},
                      {"graph", game.content_state},
                      {"native", campaign_native_state(game)},
                      {"world_snapshot", false},
                      {"world_digest", ""},
                      {"snapshot_mission", ""},
                      {"persistence_group", ""},
                      {"base_digest", content_base_digest()},
                      {"simulation_options", content_simulation_options()}};
        cc::content::save_bundle(path, state, {}, *content);
        game.content_checkpoint_expected = true;
        return;
    }
    auto saved = cc::content::load_bundle(path, *content);
    validate_save_environment(saved.state);
    if (saved.state.at("graph").value("run", std::string{}) != game.content_run)
        fail("campaign-run", "Existing checkpoint belongs to a different campaign run");
    if (saved.state.value("campaign", std::string{}) != game.content_campaign ||
        saved.state.value("mission", std::string{}) != game.content_mission)
        fail("campaign-state", "Existing snapshot belongs to a different campaign or mission");
    // Briefing salvage, lance assignments and Mech Lab edits are durable pilot
    // state. Update them atomically with the previously accepted world bytes;
    // another historical game's save must never capture the current live world.
    saved.state["graph"] = game.content_state;
    saved.state["native"] = campaign_native_state(game);
    cc::content::save_bundle(path, saved.state, saved.world, *content);
    game.content_checkpoint_expected = true;
}
void read_content_campaign(CampaignGame &game) {
    const auto path = bundle_path(game);
    if (!cc::content::fs::exists(path)) {
        if (game.campaign > 4 || active_content())
            fail("save-bundle",
                 "Managed campaign world exists without its required identity bundle");
        return;
    }
    const auto content = active_content();
    if (!content)
        fail("save-content", "Managed campaign needs its content profile before reading the pilot");
    const auto saved = cc::content::load_bundle(path, *content);
    apply_content_campaign_state(game, saved.state);
}
void apply_content_campaign_state(CampaignGame &game, const Json &state) {
    const auto content = active_content();
    if (!content)
        fail("campaign-content", "Managed campaign has no active content set");
    validate_save_environment(state);
    cc::content::validate_saved_campaign_graph(state, *content);
    auto campaign_id = state.value("campaign", std::string{});
    auto mission_id = state.at("mission").get<std::string>();
    auto graph = state.value("graph", Json::object());
    if (!cc::content::valid_campaign_run_id(game.content_run) ||
        graph.value("run", std::string{}) != game.content_run)
        fail("campaign-run", "Checkpoint does not match the pilot index's expected campaign run");
    if (!content->find("mission", mission_id))
        fail("save-missing-content", "Managed campaign content is unavailable");
    if (campaign_id == "stock:campaign" && (graph.at("stock_operation") != game.operation() ||
                                            graph.at("stock_mission") != game.mission_number()))
        fail("campaign-state", "Saved stock campaign progression differs from the pilot record");
    restore_campaign_native_state(game, state.at("native"));
    game.content_campaign.swap(campaign_id);
    game.content_mission.swap(mission_id);
    game.content_state.swap(graph);
    game.content_checkpoint_expected = true;
}
bool restore_content_campaign(CampaignGame &game) {
    const auto content = active_content();
    if (!content)
        fail("campaign-content", "Managed campaign has no active content set");
    const auto path = bundle_path(game);
    if (!cc::content::fs::exists(path)) {
        if (game.content_checkpoint_expected)
            fail("save-bundle", "The campaign's accepted checkpoint is missing");
        return false;
    }
    const auto saved = cc::content::load_bundle(path, *content);
    validate_save_environment(saved.state);
    if (saved.state.at("graph").value("run", std::string{}) != game.content_run)
        fail("campaign-run", "Native checkpoint belongs to a different campaign run");
    if (saved.state.value("campaign", std::string{}) != game.content_campaign ||
        saved.state.value("mission", std::string{}) != game.content_mission)
        fail("campaign-state",
             "Native restoration checkpoint belongs to a different campaign or mission");
    if (content_stock_campaign(game) && (game.b0 || game.mission_number() == 1 ||
                                         (game.operation() == 4 && game.mission_number() == 4)))
        return false;
    if (saved.state.contains("world_snapshot") && !saved.state["world_snapshot"].is_boolean())
        fail("save-native", "Saved world snapshot marker is malformed");
    if (!saved.state.value("world_snapshot", true)) {
        if (!saved.world.empty())
            fail("save-native", "Semantic-only checkpoint contains unexpected world bytes");
        return false;
    }
    // Explicit boundary migrations can discard the old world while retaining
    // semantic inventory/progression; they also begin a fresh native world.
    if (saved.world.empty())
        return false;
    const auto *mission = content->find("mission", game.content_mission);
    // World state crosses a mission boundary only when both authors explicitly
    // identify a shared persistence group. Geometry and native resources still
    // require exact identity; inventory/graph state remains durable either way.
    const auto group = mission->data.value("persistence_group", std::string{});
    const bool same_mission =
        saved.state.value("snapshot_mission", std::string{}) == game.content_mission;
    const bool shared_group =
        !group.empty() && saved.state.value("persistence_group", std::string{}) == group;
    if (!mission->data.value("persist_world", false) || (!same_mission && !shared_group))
        return false;
    if (saved.state.value("world_digest", std::string{}) != content_world_digest()) {
        if (same_mission)
            fail("save-world",
                 "The accepted mission snapshot does not match the loaded native world");
        return false;
    }
    const auto relative = std::string(game.path()) + ".restore";
    const auto temporary = zutl::write_path(relative.c_str());
    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::error_code error;
            cc::content::fs::remove(path, error);
        }
    } cleanup{temporary};
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(saved.world.data()),
                  static_cast<std::streamsize>(saved.world.size()));
        if (!out)
            fail("save-write", "Cannot stage native world restoration");
    }
    const bool restored = pilot_hooks().load_world(relative.c_str(), 2);
    if (!restored)
        fail("save-world",
             "Native world restoration failed; the accepted checkpoint was preserved");
    return restored;
}
bool content_campaign_terminal() {
    const auto *p = current_pilot();
    const auto *game = p && p->current ? dynamic_cast<const CampaignGame *>(p->current) : nullptr;
    return game && !game->content_mission.empty() &&
           (content_stock_campaign(*game) ? game->content_state.value("terminal", false)
                                          : g_terminal);
}
bool consume_content_initial_launch() {
    const bool pending = g_initial_launch;
    g_initial_launch = false;
    return pending;
}

void append_content_campaign_choices(Pilot &pilot) {
    const auto content = active_content();
    if (!content)
        return;
    auto add = [&](const Definition &definition, bool campaign) {
        for (const auto *entry : pilot.campaigns)
            if (const auto *game = dynamic_cast<const CampaignGame *>(entry);
                game && game->content_placeholder &&
                (campaign
                     ? game->content_campaign == definition.id
                     : game->content_campaign.empty() && game->content_mission == definition.id))
                return;
        auto *game = new CampaignGame(&pilot, operation_alias(pilot, definition.id), 1);
        game->content_placeholder = true;
        if (campaign) {
            game->content_campaign = definition.id;
            game->content_state = begin_content_campaign(definition);
            game->content_mission =
                node_for(definition, game->content_state.at("node").get<std::string>())
                    .at("mission")
                    .get<std::string>();
        } else
            game->content_mission = definition.id;
        bind_content_campaign_run(*game);
        pilot.campaigns.push_back(game);
    };
    for (const auto *campaign : content->of_kind("campaign"))
        if (campaign->behavior == "graph")
            add(*campaign, true);
    for (const auto *mission : content->of_kind("mission"))
        if (mission->owner != "stock" && !mission->data.value("multiplayer", false) &&
            mission->data.value("selectable", true))
            add(*mission, false);
}
CampaignGame &select_content_standalone(Pilot &pilot, const std::string &mission) {
    const auto content = active_content();
    const auto *definition = content ? content->find("mission", mission) : nullptr;
    if (!definition || definition->data.value("multiplayer", false))
        fail("mission-selection", "Standalone mission is unavailable");
    append_content_campaign_choices(pilot);
    for (auto *entry : pilot.campaigns)
        if (auto *game = dynamic_cast<CampaignGame *>(entry); game && game->content_placeholder &&
                                                              game->content_campaign.empty() &&
                                                              game->content_mission == mission) {
            pilot.current = game;
            return *game;
        }
    fail("mission-selection", "Standalone mission is not selectable");
}
std::string content_campaign_label(const CampaignGame &game) {
    const auto content = active_content();
    if (!content)
        return game.content_mission;
    const auto *definition =
        content->find(game.content_campaign.empty() ? "mission" : "campaign",
                      game.content_campaign.empty() ? game.content_mission : game.content_campaign);
    const auto name =
        definition ? definition->data.value("name", definition->id) : game.content_mission;
    return game.content_placeholder
               ? (game.content_campaign.empty() ? "Mission: " : "New campaign: ") + name
               : name + " - " + game.content_mission;
}
} // namespace mech3
