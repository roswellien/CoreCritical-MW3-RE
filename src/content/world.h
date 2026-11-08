#pragma once

#include "content/content.h"

namespace cc::content {

// Authored worlds compile to portable triangles and named instances, never
// serialized host pointers or a copy of a retail GameZ world. Runtime adapters
// construct the existing native simulation world from this checked snapshot.
Json compile_world(const Definition &definition, const ResolvedContentSet &content);
// Checks authored field names without decoding any source geometry or images.
void validate_world_fields(const Definition &definition);
void validate_compiled_world_fields(const Json &world);
void validate_compiled_world(const Json &world);

} // namespace cc::content
