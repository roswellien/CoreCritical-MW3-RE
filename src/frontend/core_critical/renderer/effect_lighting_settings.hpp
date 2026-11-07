#pragma once
#include "effect_lighting.hpp"
#include "sun_lighting.hpp"
namespace mw34::effects {
struct Settings {bool enabled{},fixtures{true},fixture_shadows{};unsigned fixture_shadow_size{256};float fixture_retain_distance{160},fixture_fade_distance{32};unsigned limit{64},weapon_reserve{0};float strength{1},night_gain{1},fixture_range_scale{1};};
inline const Settings& settings(){
 static const Settings s=[] {
  Settings v;v.enabled=sun::flag("MW34_EFFECT_LIGHTS");
  const float limit=sun::parameter("MW34_EFFECT_LIGHT_LIMIT",64,1,capacity);
  if(limit!=std::floor(limit))throw std::runtime_error("Effect light limit must be an integer");
  v.limit=unsigned(limit);v.strength=sun::parameter("MW34_EFFECT_LIGHT_STRENGTH",1,0,8);
  v.fixtures=!std::getenv("MW34_FIXTURE_LIGHTS")||sun::flag("MW34_FIXTURE_LIGHTS");
  const float reserve=sun::parameter("MW34_WEAPON_LIGHT_RESERVE",0,0,capacity);
  if(reserve!=std::floor(reserve))throw std::runtime_error("Weapon reserve must be an integer");
  v.weapon_reserve=std::min(unsigned(reserve),v.limit);
  v.fixture_range_scale=sun::parameter("MW34_FIXTURE_LIGHT_RANGE_SCALE",1,.25f,4);
  v.fixture_shadows=sun::flag("MW34_FIXTURE_SHADOWS");
  const float shadowSize=sun::parameter("MW34_FIXTURE_SHADOW_SIZE",256,128,512);
  if(shadowSize!=128&&shadowSize!=256&&shadowSize!=512)throw std::runtime_error("Fixture shadow size must be 128, 256 or 512");
  v.fixture_shadow_size=unsigned(shadowSize);
  v.fixture_retain_distance=sun::parameter("MW34_FIXTURE_LIGHT_RETAIN_DISTANCE",160,32,2000);
  v.fixture_fade_distance=sun::parameter("MW34_FIXTURE_LIGHT_FADE_DISTANCE",32,1,256);
  if(v.fixture_fade_distance>=v.fixture_retain_distance)throw std::runtime_error("Fixture fade band must be smaller than retention distance");
  v.night_gain=sun::parameter("MW34_NIGHT_WORLD_GAIN",1,0,1);
  if((v.enabled||v.night_gain!=1)&&!sun::settings().enabled)throw std::runtime_error("Effect/night lighting requires MW34_SUN=1");
  return v;
 }();return s;
}
}
