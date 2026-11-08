#pragma once

#include <optional>

#include "content/content.h"

namespace cc::content {

// Adapt old data packages without modifying their source directory. A schema-3
// manifest returned by this adapter can be installed by the ordinary store.
std::optional<Package> try_load_legacy(const fs::path &root, bool verify_files = true);
void resolve_legacy_claims(ResolvedContentSet &content, const Json &profile);
bool legacy_file_allowed(const std::string &path);
bool legacy_file_is_gameplay(const std::string &path);

} // namespace cc::content
