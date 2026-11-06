#include "gamez/znetwork/znet_object.h"

#include <cstddef>

namespace znet {

Registry& registry() {
  static Registry r;  // 0x00603fd8 (ctor 0x004c7b80)
  return r;
}

// Mech3.exe 1.2 @ 0x0058aec0
Object::Object(int32_t i) : id(i), id_byte(uint8_t(i)) { registry().list.push_back(this); }

// Mech3.exe 1.2 @ 0x0058b000, unregistering through 0x0058b310 (skipped while
// the registry is being cleared). The elements are the derived class's.
Object::~Object() {
  Registry& r = registry();
  if (!r.clearing) r.list.remove(this);
}

// Mech3.exe 1.2 @ 0x0058b060
Element* Object::elem(int32_t i) const {
  if (i >= 0 && uint32_t(i) < elems.size()) return elems[std::size_t(i)];
  // The exe asks element 0 unchecked; an empty object has none (FIX (bug):
  // the exe reads past an empty vector).
  return elems.empty() || !elems[0] ? nullptr : elems[0]->miss();
}

// Mech3.exe 1.2 @ 0x0058b130
int32_t Object::index_of(const Element* e) const {
  for (std::size_t i = 0; i < elems.size(); ++i)
    if (elems[i] == e) return int32_t(i);
  return -1;
}

// Mech3.exe 1.2 @ 0x0058b160
Object* find(int32_t id) {
  for (Object* o : registry().list)
    if (o->get_id() == id) return o;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0058b1c0
int32_t alloc_id(int32_t want, int32_t lo, int32_t hi) {
  if (want < 0) want = lo;
  int32_t i = (want < lo || want >= hi) ? lo : want;
  if (i != want && !find(want)) return want;
  int32_t got;
  Object* used;
  do {
    got = i;
    used = find(got);
    if (!used) return got;
    if (++i == hi) i = lo;
  } while (i != lo);
  return used ? -1 : got;
}

// Mech3.exe 1.2 @ 0x0058b290: the AI's regions (ids from 10000) outlive it;
// their own list frees them (zainet_region.cpp).
void clear() {
  Registry& r = registry();
  r.clearing = 1;
  for (Object*& o : r.list) {
    if (o->get_id() < kKeptIds) {
      delete o;
      o = nullptr;
    }
  }
  r.clearing = 0;
  r.list.remove(nullptr);  // 0x0058b310(0)
}

}  // namespace znet
