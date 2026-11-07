#pragma once
#include "content/content.h"

namespace cc::content {
struct SavedState {
    Json state;
    std::vector<uint8_t> world;
};
// Opaque identity for one new campaign run, retained across saves/migrations.
std::string new_campaign_run_id(const std::string &campaign);
bool valid_campaign_run_id(const std::string &run);
void save_bundle(const fs::path &path, const Json &state, const std::vector<uint8_t> &world,
                 const ResolvedContentSet &resolved);
SavedState load_bundle(const fs::path &path, const ResolvedContentSet &resolved);
// Checks final campaign/node/mission and bounded progression state without
// publishing native state. Shared by native reads and migration previews.
void validate_saved_campaign_graph(const Json &state, const ResolvedContentSet &resolved);
// Migration declarations bind exact source/target locks. Source is preserved;
// output must be a distinct new file and a verified backup is retained.
Json migrate_bundle(const fs::path &source, const fs::path &output, const Json &migration,
                    const ResolvedContentSet &resolved, bool preview = false);
} // namespace cc::content
