#pragma once

#include "content/content.h"

namespace mech3 {
class CampaignGame;
bool content_stock_campaign(const CampaignGame &game);
// Binds the existing native campaign without replacing its progression rules.
bool adopt_content_stock_campaign(CampaignGame &game);
void carry_content_stock_campaign(const CampaignGame &from, CampaignGame &to);
void finish_content_stock_campaign(CampaignGame &game);
void validate_content_stock_campaign(const CampaignGame &game);
} // namespace mech3
