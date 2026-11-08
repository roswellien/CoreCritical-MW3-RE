#include <cstdint>
#include <cstdio>
#include <string>

#include "check.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"

namespace {
zrdr::Node* text(const char* source) {
  std::string path = std::string(MW3_TEST_TMP) + "/effect_test.zrd";
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return nullptr;
  std::fputs(source, f);
  std::fclose(f);
  f = std::fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  zrdr::Node* root = zrdr::parse_text(f);
  std::fclose(f);
  return root;
}
}  // namespace

TEST(weapon_effect_sound_counts_match_the_owned_native_slots) {
  const auto previous_init = zwep::init_hooks();
  const auto previous_run = zwep::run_hooks();
  struct Restore {
    zwep::InitHooks init;
    zwep::RunHooks run;
    ~Restore() {
      zwep::init_hooks() = init;
      zwep::run_hooks() = run;
    }
  } restore{previous_init, previous_run};

  zwep::init_hooks().sound = [](const char* name) -> void* {
    return reinterpret_cast<void*>(uintptr_t(name[1] - '0') + (name[0] == 'b' ? 256 : 0));
  };

  auto* root = text("effect (SOUND (s1 s2 s3 s4 s5 s6) BOUNCE_SOUND (b1 b2 b3 b4 b5 b6))");
  EXPECT(root != nullptr);
  if (!root) return;

  zwep::Effect effect;
  zwep::read_effect(root, &effect, "effect");
  zrdr::free_node(root);

  EXPECT(effect.sound_count == 4 && effect.bounce_sound_count == 4);

  zwep::WeaponDef definition;
  definition.fire = effect;
  EXPECT(!zwep::set_fire_sound(&definition, 4));
  EXPECT(!zwep::set_fire_sound(&definition, -2));
  EXPECT(zwep::set_fire_sound(&definition, -1));
  EXPECT(zwep::set_fire_sound(&definition, 3));

  if (effect.sound_count != 4 || effect.bounce_sound_count != 4) return;

  static bool valid;
  static uint32_t seen;
  valid = true;
  seen = 0;
  zwep::run_hooks().sound_at = [](void* sound, const zwep::Vec3f3*, float) -> void* {
    const auto value = reinterpret_cast<uintptr_t>(sound);
    const auto slot = value > 256 ? value - 256 : value;
    valid &= slot >= 1 && slot <= 4;
    if (slot >= 1 && slot <= 4) seen |= 1u << (slot - 1 + (value > 256 ? 4 : 0));
    return nullptr;
  };

  definition.impacts = &effect;
  zwep::HitRecord hit;
  for (int i = 0; i < 512; ++i) {
    zwep::sound(&definition, &hit, 0, 1.0f);
    zwep::bounce_sound(&definition, &hit, 0, 1.0f);
  }
  EXPECT(valid && seen == 255);
}

TEST(weapon_impacts_use_default_for_unset_and_unavailable_soils) {
  struct Restore {
    zwep::HitHooks hooks = zwep::hit_hooks();
    zwep::HitState hit = zwep::hit_state();
    zwep::RunHooks run = zwep::run_hooks();
    ~Restore() {
      zwep::hit_hooks() = hooks;
      zwep::hit_state() = hit;
      zwep::run_hooks() = run;
    }
  } restore;

  zwep::hit_hooks() = {};
  zwep::hit_state() = {};

  static int32_t selected_soil, played_effect, sounds;
  zwep::hit_hooks().soil = [](void* material) {
    return static_cast<int32_t>(reinterpret_cast<intptr_t>(material));
  };
  zwep::hit_hooks().sound = [](zwep::WeaponDef*, zwep::HitRecord*, int32_t soil, float) {
    selected_soil = soil;
  };
  zwep::hit_hooks().impact_effect = [](int32_t effect, const zwep::Vec3f3*) {
    played_effect = effect;
  };

  // Guard entries make the old negative/out-of-range read observable without
  // invoking undefined memory outside the fixture's allocation.
  zwep::Effect storage[10];
  for (auto& effect : storage) effect.effect = 99;

  zwep::WeaponDef definition;
  definition.impacts = storage + 1;
  definition.impacts[0].effect = 17;
  definition.impacts[5].effect = 29;
  definition.impacts[7].effect = 31;

  zwep::Projectile projectile;
  zwep::HitRecord hit;
  for (const int32_t soil : {-1, 6, 7, 5}) {
    selected_soil = -100;
    played_effect = 0;
    hit.material = reinterpret_cast<void*>(intptr_t(soil));
    zwep::impact(&definition, &hit, &projectile, nullptr);
    EXPECT(selected_soil == (soil == 5 ? 5 : 0));
    EXPECT(played_effect == (soil == 5 ? 29 : 17));
  }

  definition.impact_count = 8;
  hit.material = reinterpret_cast<void*>(intptr_t(7));
  zwep::impact(&definition, &hit, &projectile, nullptr);
  EXPECT(selected_soil == 7 && played_effect == 31);

  definition.impacts[0].sound_count = 1;
  definition.impacts[0].sounds[0] = reinterpret_cast<void*>(uintptr_t(42));
  definition.impacts[0].bounce_sound_count = 1;
  definition.impacts[0].bounce_sounds[0] = reinterpret_cast<void*>(uintptr_t(43));

  definition.impacts[7].sound_count = 1;
  definition.impacts[7].sounds[0] = reinterpret_cast<void*>(uintptr_t(45));
  definition.impacts[7].bounce_sound_count = 1;
  definition.impacts[7].bounce_sounds[0] = reinterpret_cast<void*>(uintptr_t(44));

  static uintptr_t last_sound = 0;
  sounds = 0;
  zwep::run_hooks().sound_at = [](void* sound, const zwep::Vec3f3*, float) -> void* {
    last_sound = reinterpret_cast<uintptr_t>(sound);
    ++sounds;
    return nullptr;
  };

  // Direct numeric calls with negative/out-of-bounds soils
  // sound with soil -1 falls back to default soil 0 (sound 42)
  zwep::sound(&definition, &hit, -1, 1.0f);
  EXPECT(sounds == 1 && last_sound == 42);

  // sound with out-of-bounds soil 8 falls back to default soil 0 (sound 42)
  zwep::sound(&definition, &hit, 8, 1.0f);
  EXPECT(sounds == 2 && last_sound == 42);

  // sound with valid extended soil 7 plays extended sound 45
  zwep::sound(&definition, &hit, 7, 1.0f);
  EXPECT(sounds == 3 && last_sound == 45);

  // bounce_sound with negative soil -1 falls back to default soil 0 (sound 43)
  zwep::bounce_sound(&definition, &hit, -1, 1.0f);
  EXPECT(sounds == 4 && last_sound == 43);

  // bounce_sound with out-of-bounds soil 99 falls back to default soil 0 (sound 43)
  zwep::bounce_sound(&definition, &hit, 99, 1.0f);
  EXPECT(sounds == 5 && last_sound == 43);

  // bounce_sound with valid extended soil 7 plays extended bounce sound 44
  zwep::bounce_sound(&definition, &hit, 7, 1.0f);
  EXPECT(sounds == 6 && last_sound == 44);
}
