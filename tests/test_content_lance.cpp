#include <memory>

#include "check.h"
#include "mech3/briefing.h"
#include "mech3/content_registry.h"

TEST(content_native_lance_exhausts_added_chassis_without_looping) {
    using cc::content::ResolvedContentSet;
    struct Session {
        std::shared_ptr<const ResolvedContentSet> previous = mech3::active_content();
        ~Session() { mech3::activate_content(previous); }
    } session;
    auto content = std::make_shared<ResolvedContentSet>();
    content->definitions = mech3::stock_content();
    auto chassis = *content->find("chassis", "stock:bushwacker");
    chassis.id = "authored:extra", chassis.owner = "authored", chassis.handle = 19;
    chassis.data["name"] = "authored_extra";
    chassis.data["label"] = "Authored Extra";
    chassis.data["base"] = "stock:bushwacker";
    content->definitions.push_back(std::move(chassis));
    mech3::activate_content(content);
    mech3::Store store;
    mech3::GameSlot slot(19);
    mech3::lance_slot(slot, store, false);
    EXPECT(slot.mech == -1 && slot.config == nullptr);
}
