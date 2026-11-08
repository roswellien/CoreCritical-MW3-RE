#pragma once

#include "content/content.h"

namespace mech3 {
class CampaignGame;

namespace campaign_items {
void validate(const cc::content::Json &action, const cc::content::Definition &campaign,
              const cc::content::ResolvedContentSet &content);
void apply(CampaignGame &game, const cc::content::Json &rewards);
} // namespace campaign_items
} // namespace mech3
