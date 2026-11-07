#pragma once
namespace zcls {
struct Node;
}
namespace zmod {
// Synchronous rendering handoff. True only after an owned submission succeeds.
bool submit_geometry(zcls::Node &node);
} // namespace zmod
