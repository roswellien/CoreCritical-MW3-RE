#pragma once

#include "content/content.h"

namespace mech3::world_readers {
// Native authoring schemas and raw reader budgets are checked before tree
// construction, independently of scene allocation and registry lifetime.
void validate_tree(const cc::content::Json &value, size_t &nodes, int depth = 0);
void validate_document(const std::string &name, const cc::content::Json &document);
} // namespace mech3::world_readers
