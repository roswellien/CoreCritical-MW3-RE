#include <memory>

#include "check.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"
#include "mech3/mech_config.h"
#include "mech3/mech_names.h"
#include "mech3/mech_salvage.h"
#include "mech3/pilot.h"

namespace {
using cc::content::Definition;
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
} // namespace

TEST(content_native_store_preserves_two_item_identities_in_the_same_family) {
    Session session;
    auto content = stock();
    content->definitions.push_back(equipment("authored:ecm_a", 15, 2.25f));
    content->definitions.push_back(equipment("authored:ecm_b", 16, 3.5f));
    mech3::activate_content(content);
    mech3::EquipmentItem first, second;
    first.equipment = 15, first.tech = 2, second.equipment = 16, second.tech = 2;
    mech3::Store store;
    store.add_copy(&first, 2);
    store.add_copy(&second, 3);
    EXPECT(store.items.size() == 2 && store.count_like(&first) == 2 &&
           store.count_like(&second) == 3);
}

TEST(content_native_salvage_retains_additional_heat_sink_identity) {
    Session session;
    auto content = stock();
    Definition sink = equipment("authored:sink", 15, 1.25f);
    sink.behavior = "mw3:equipment/sink@1";
    content->definitions.push_back(sink);
    mech3::activate_content(content);
    mech3::Config config(3, "Sinks");
    config.add_equipment(15, 2, 2);
    for (auto *item : config.items)
        item->part = 7;
    mech3::SalvageMech mech;
    mech.has_data = true, mech.type = 3, mech.config = &config, mech.sinks = 12;
    mech3::Store store;
    EXPECT(mech3::add_salvage(store, mech, {}));
    mech3::EquipmentItem probe;
    probe.equipment = 15, probe.tech = 2;
    EXPECT(store.count_like(&probe) == 2);
    probe.equipment = 9;
    EXPECT(store.count_like(&probe) == 0);
}

TEST(content_native_store_and_slot_records_keep_stable_ids_and_transactional_reads) {
    Session session;
    auto first = stock();
    first->definitions.push_back(equipment("authored:ecm", 15, 2.0f));
    mech3::activate_content(first);
    mech3::Store source;
    source.add_equipment(15, 2, 3);
    source.items.front()->b10 = 1;
    using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
    File file(std::tmpfile(), std::fclose);
    EXPECT(file != nullptr);
    if (!file)
        return;
    const auto bytes = source.save(file.get());
    EXPECT(bytes > 12);
    auto second = stock();
    second->definitions.push_back(equipment("authored:new_ecm", 15, 1.0f));
    second->definitions.push_back(equipment("authored:ecm", 16, 2.0f));
    mech3::activate_content(second);
    std::rewind(file.get());
    mech3::Store restored;
    restored.load(file.get());
    mech3::EquipmentItem wanted;
    wanted.equipment = 16;
    wanted.tech = 2;
    EXPECT(restored.count_like(&wanted) == 3 && restored.items.front()->b10 == 1);
    wanted.equipment = 15;
    EXPECT(restored.count_like(&wanted) == 0);
    File broken(std::tmpfile(), std::fclose);
    EXPECT(broken != nullptr);
    if (!broken)
        return;
    std::fwrite("CCSTRV1\n", 1, 8, broken.get());
    std::rewind(broken.get());
    bool rejected = false;
    try {
        restored.load(broken.get());
    } catch (const cc::content::Error &) {
        rejected = true;
    }
    wanted.equipment = 16;
    EXPECT(rejected && restored.count_like(&wanted) == 3);
    mech3::GameSlot original(-1, nullptr, 0);
    original.x0c = 8;
    original.x14 = 12;
    File slot_file(std::tmpfile(), std::fclose);
    EXPECT(slot_file != nullptr);
    if (!slot_file)
        return;
    mech3::save_managed_slot(slot_file.get(), original);
    std::rewind(slot_file.get());
    mech3::GameSlot copy(-1, nullptr, 0);
    mech3::load_managed_slot(slot_file.get(), copy);
    EXPECT(copy.mech == -1 && copy.x0c == 8 && copy.x14 == 12 && !copy.config);
}
