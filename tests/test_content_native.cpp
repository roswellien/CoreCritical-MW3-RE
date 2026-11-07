#include <cstring>
#include <fstream>
#include <memory>

#include "check.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/content_registry.h"
#include "mech3/mech.h"
#include "mech3/mech_build.h"
#include "mech3/mech_config.h"
#include "mech3/mech_data.h"
#include "mech3/mech_names.h"
#include "mech3/mech_objective.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/veh_basic_object.h"
#include "mech3/weapon.h"

namespace {
using cc::content::Definition;
using cc::content::Json;
using cc::content::ResolvedContentSet;
struct Session {
    std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
    ~Session() { mech3::activate_content(previous); }
};
std::shared_ptr<ResolvedContentSet> stock() {
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    return content;
}
Definition equipment(const char *id, int32_t handle, float tons) {
    return {"equipment",
            id,
            "authored",
            "mw3:equipment/ecm@1",
            {{"name", std::string("ecm_") + std::to_string(handle)},
             {"label", id},
             {"tech", 2},
             {"weight", {tons, tons}},
             {"slots", {3, 3}}},
            handle};
}
zrdr::Node *text(const char *source) {
    std::FILE *file = std::tmpfile();
    if (!file)
        return nullptr;
    std::fwrite(source, 1, std::strlen(source), file);
    std::rewind(file);
    zrdr::Node *root = zrdr::parse_text(file);
    std::fclose(file);
    return root;
}
} // namespace

TEST(content_native_equipment_identity_weight_slots_and_family_lifecycle) {
    Session session;
    auto content = stock();
    content->definitions.push_back(equipment("authored:ecm_a", 15, 2.25f));
    content->definitions.push_back(equipment("authored:ecm_b", 16, 3.5f));
    mech3::activate_content(content);
    EXPECT(mech3::equip_kind("authored:ecm_a") == 15);
    EXPECT(mech3::equip_kind("ecm_16") == 16);
    EXPECT(mech3::equipment_family(15) == 8 && mech3::equipment_family(16) == 8);
    EXPECT(mech3::equip_weight(15, 2) == 2.25f && mech3::equip_slots(15, 2) == 3);

    mech3::Config config(3, "Authored");
    auto *first = config.add_equipment(15, 2, 1);
    auto *second = config.add_equipment(16, 2, 1);
    first->part = 7, second->part = 8;
    EXPECT(!first->same_as(*second));
    EXPECT(first->weight(-1) == 2.25 && second->weight(-1) == 3.5);
    config.recount();
    EXPECT(config.free_in[7] == 9 && config.free_in[8] == 9);

    mech3::WeaponManager weapons(nullptr);
    mech3::Ams ams(150.0f, 0.6f, 100);
    mech3::Config *kept = nullptr;
    int32_t counts[15] = {};
    mech3::configure_weapons(nullptr, &weapons, &config, &kept, counts, &ams, nullptr);
    EXPECT(kept == &config && counts[8] == 2);
    mech3::Mech mech;
    mech3::MechData data;
    data.roster = 3;
    mech.pilot = &data;
    mech.config = &config;
    std::memcpy(mech.equipment, counts, sizeof counts);
    mech3::MechPart part;
    part.mech = &mech, part.id = 7;
    const auto saved_calls = mech3::damage_calls();
    mech3::damage_calls() = {};
    part.lose_contents();
    mech3::damage_calls() = saved_calls;
    EXPECT(mech.equipment[8] == 1); // the other distinct item still contributes
    EXPECT(first->equipment == 15 && second->equipment == 16);
}

TEST(content_native_additive_weapon_uses_native_reader_and_preserves_source) {
    Session session;
    auto content = stock();
    content->definitions.push_back({"weapon",
                                    "authored:ac5",
                                    "authored",
                                    "mw3:weapon@1",
                                    {{"name", "authored_ac5"},
                                     {"label", "Authored AC5"},
                                     {"base", "stock:ac5"},
                                     {"tech", 2},
                                     {"uses_ammo", true},
                                     {"missile", false},
                                     {"lrm", false},
                                     {"reader", {{"DAMAGE", Json::array({42.0})}}}},
                                    38});
    mech3::activate_content(content);
    EXPECT(mech3::weapon_index("authored:ac5") == 38);
    EXPECT(mech3::weapon_count() == 39 && mech3::weapon_uses_ammo(38));
    zrdr::Node *root = text("BALLISTICS (AC5 (NAME (\"AC5\") DAMAGE (5.0)))");
    EXPECT(root != nullptr);
    if (!root)
        return;
    root = mech3::compose_content_reader("weapons.zrd", root);
    auto *ballistics = zrdr::find(root, "BALLISTICS");
    auto *original = zrdr::find_flat(ballistics, "AC5", 1);
    auto *added = zrdr::find_flat(ballistics, "authored_ac5", 1);
    EXPECT(original && added);
    EXPECT(original && zrdr::find(original, "DAMAGE")->list[1].f == 5.0f);
    EXPECT(added && zrdr::find(added, "DAMAGE")->list[1].f == 42.0f);
    EXPECT(added && std::strcmp(zrdr::get_string(added, "NAME"), "authored_ac5") == 0);
    zrdr::free_node(root);
    struct Hooks : mech3::ObjectiveHooks {
        const char *localize(const char *) override { return "Authored objective"; }
        zgame::Handle sound_find(const char *) override { return 0; }
    } hooks;
    root = text("weapon_fire (weapon (authored_ac5))");
    auto *args = root ? zrdr::find(root, "weapon_fire") : nullptr;
    auto condition = args ? mech3::parse_condition(args - 1, hooks) : nullptr;
    auto *fire = dynamic_cast<mech3::WeaponObjective *>(condition.get());
    EXPECT(fire && fire->id == 38);
    zrdr::free_node(root);
}

TEST(content_native_rejects_unsupported_behavior_and_reader_types) {
    auto content = stock();
    Definition bad = equipment("authored:bad", 15, 2.0f);
    bad.behavior = "mw3:equipment/ecm_aura@1";
    content->definitions.push_back(bad);
    bool rejected = false;
    try {
        mech3::validate_native_content(*content);
    } catch (const cc::content::Error &e) {
        rejected = e.diagnostic.field == "behavior";
    }
    EXPECT(rejected);
    content->definitions.back() = {"weapon",
                                   "authored:bad",
                                   "authored",
                                   "mw3:weapon@1",
                                   {{"name", "bad_weapon"},
                                    {"label", "Bad Weapon"},
                                    {"base", "stock:ac5"},
                                    {"tech", 2},
                                    {"uses_ammo", true},
                                    {"missile", false},
                                    {"lrm", false},
                                    {"reader", {{"DAMAGE", true}}}},
                                   38};
    rejected = false;
    try {
        mech3::validate_native_content(*content);
    } catch (const cc::content::Error &e) {
        rejected = e.diagnostic.field == "reader";
    }
    EXPECT(rejected);
    content->definitions.back().data["reader"] = {{"DAMAGE", {1e100}}};
    rejected = false;
    try {
        mech3::validate_native_content(*content);
    } catch (const cc::content::Error &e) {
        rejected = e.diagnostic.field == "reader";
    }
    EXPECT(rejected);
}

TEST(content_native_loadout_lab_reads_real_composed_data_before_world_weapon_initialization) {
    Session session;
    struct Readers {
        zutl::Mounts mounts = zutl::mounts();
        zrdr::State readers = zrdr::state();
        Readers() {
            zutl::mounts() = {};
            zrdr::state() = {};
        }
        ~Readers() {
            zutl::mounts() = mounts;
            zrdr::state() = readers;
        }
    } readers;
    auto root = cc::content::fs::path(MW3_TEST_TMP) / "content_weapon_lab";
    cc::content::fs::create_directories(root);
    {
        std::ofstream source(root / "weapons.zrd");
        source << "VERSION (2) BALLISTICS (AC5 (NAME (\"AC5\") MASS (clan (4.0 1.0) sphere (5.0 "
                  "2.0) ammo (20)) SALVO (count (3) truecount (6))))";
    }
    zutl::mount(root.generic_string());
    auto content = stock();
    content->definitions.push_back({"weapon",
                                    "authored:lab_ac5",
                                    "authored",
                                    "mw3:weapon@1",
                                    {{"name", "authored_lab_ac5"},
                                     {"label", "Authored lab AC5"},
                                     {"base", "stock:ac5"},
                                     {"tech", 2},
                                     {"uses_ammo", true},
                                     {"missile", false},
                                     {"lrm", false},
                                     {"reader", {{"MASS", {{"sphere", {7.0, 3.0}}}}}}},
                                    38});
    mech3::activate_content(content);
    const auto runtime_count = zwep::state().defs.size();
    EXPECT(mech3::weapon_type("authored_lab_ac5") == nullptr);
    mech3::prepare_content_weapon_labs();
    EXPECT(mech3::weapon_weight(38, 2) == 7.0f && mech3::weapon_slots(38, 2, false) == 3);
    EXPECT(mech3::content_weapon_rounds(38) == 120 && mech3::ammo_weight(38) == 1.0f / 120.0f);
    EXPECT(mech3::weapon_type("authored_lab_ac5") == nullptr &&
           zwep::state().defs.size() == runtime_count);
}

TEST(content_native_vehicle_unbinds_its_node_before_freeing_the_tree) {
    struct Hooks : mech3::VehBasicHooks {
        bool unbound = false, freed = false;
        void node_detach(mech3::Handle) override {}
        void entity_set_node(mech3::Vehicle &vehicle, mech3::Handle node) override {
            if (vehicle.node && !node)
                unbound = !freed;
            vehicle.node = node;
        }
        void entity_unregister(mech3::Vehicle &) override {}
        void world_remove(mech3::Handle) override {}
        void node_free(mech3::Handle) override {
            EXPECT(unbound);
            freed = true;
        }
        void zentity_set_node(mech3::Vehicle &vehicle, mech3::Handle node) override {
            vehicle.node = node;
        }
    } hooks;
    struct HookSession {
        ~HookSession() { mech3::set_veh_basic_hooks(nullptr); }
    } restore;
    mech3::set_veh_basic_hooks(&hooks);
    mech3::BasicVehicle vehicle;
    mech3::vehicle_base(vehicle);
    vehicle.node = reinterpret_cast<void *>(uintptr_t(1));
    mech3::vehicle_destroy(vehicle);
    EXPECT(hooks.unbound && hooks.freed && vehicle.node == nullptr);
}

TEST(content_native_vehicle_definition_reaches_reader_and_requires_ai) {
    Session session;
    auto content = stock();
    content->definitions.push_back(
        {"vehicle",
         "authored:truck",
         "authored",
         "mw3:ai-vehicle@1",
         {{"name", "demotruck"},
          {"label", "Demo Truck"},
          {"base", "stock:ttruck"},
          {"model_name", "ttruck"},
          {"reader", {{"common_mode", {{"health", Json::array({250.0})}}}}}},
         5});
    mech3::activate_content(content);
    EXPECT(std::strcmp(mech3::content_vehicle_model("demotruck"), "ttruck") == 0);
    zrdr::Node *root = text("ttruck (common_mode (health (100.0)))");
    EXPECT(root != nullptr);
    if (root) {
        root = mech3::compose_content_reader("vehicle.zrd", root);
        zrdr::Node *added = zrdr::find(root, "demotruck");
        EXPECT(added && zrdr::find(added, "health")->list[1].f == 250.0f);
        zrdr::free_node(root);
    }
    bool rejected = false;
    try {
        mech3::vehicle_create(nullptr, "demotruck", "demotruck_01");
    } catch (const cc::content::Error &error) {
        rejected = error.diagnostic.code == "vehicle-controller";
    }
    EXPECT(rejected);
}

TEST(content_native_skin_registers_slots_against_its_chassis) {
    Session session;
    auto content = stock();
    content->definitions.push_back({"skin",
                                    "authored:paint",
                                    "authored",
                                    "mw3:skin@1",
                                    {{"name", "demo_paint"},
                                     {"label", "Demo Paint"},
                                     {"chassis", "stock:bushwacker"},
                                     {"entries", {"authored_a", "authored_b"}}},
                                    0});
    mech3::activate_content(content);
    mech3::MechDfn dfn(3, nullptr);
    dfn.skins = new mech3::SkinSet(1);
    auto *base = new mech3::Skin("base", 2);
    base->add_entry("source_a", nullptr);
    base->add_entry("source_b", nullptr);
    dfn.skins->add(base);
    mech3::apply_content_skins(dfn);
    auto *added = dfn.skins->named("demo_paint");
    EXPECT(dfn.skins->skins.size() == 2 && added && added->entries.size() == 2);
    EXPECT(added && std::strcmp(added->entries[0]->name, "authored_a") == 0);
}

TEST(content_native_stock_adapter_preserves_catalogue_parameters_and_original_ppc) {
    Session session;
    mech3::activate_content(nullptr);
    std::vector<float> before;
    for (int32_t i = 0; i < 19; ++i) {
        before.push_back(mech3::max_mass(i));
        before.push_back(static_cast<float>(mech3::mech_tech(i)));
        before.push_back(static_cast<float>(mech3::weight_class(i)));
    }
    for (int32_t i = 0; i < 15; ++i)
        for (int32_t tech : {1, 2}) {
            before.push_back(mech3::equip_weight(i, tech));
            before.push_back(static_cast<float>(mech3::equip_slots(i, tech)));
            before.push_back(mech3::equip_has_tech(i, tech) ? 1.0f : 0.0f);
        }
    mech3::activate_content(stock());
    std::vector<float> after;
    for (int32_t i = 0; i < 19; ++i) {
        after.push_back(mech3::max_mass(i));
        after.push_back(static_cast<float>(mech3::mech_tech(i)));
        after.push_back(static_cast<float>(mech3::weight_class(i)));
    }
    for (int32_t i = 0; i < 15; ++i)
        for (int32_t tech : {1, 2}) {
            after.push_back(mech3::equip_weight(i, tech));
            after.push_back(static_cast<float>(mech3::equip_slots(i, tech)));
            after.push_back(mech3::equip_has_tech(i, tech) ? 1.0f : 0.0f);
        }
    EXPECT(before == after);
    const bool bugs = zfix::options().bugs;
    zfix::options().bugs = false;
    EXPECT(mech3::weapon_count() == 37 && mech3::weapon_index("PPC") == -1 &&
           mech3::weapon_name_message(37) == -1);
    zfix::options().bugs = true;
    EXPECT(mech3::weapon_count() == 38 && mech3::weapon_index("PPC") == 37);
    zfix::options().bugs = bugs;
}
