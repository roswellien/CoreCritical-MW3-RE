#include "renderer_bridge_config.hpp"

#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace mw34::sun {
struct Settings {
  bool enabled{}, normals{}, shadows{}, shadow_debug{}, weather_enabled{true};
  float azimuth{45}, elevation{35}, strength{.85f}, ambient{.30f},
      normal_strength{1.0f}, ground_normal_strength{1.0f};
  // Optional sun shadow map: half-width of the square sun-space region in
  // world units, depth-map resolution, receiver bias and shadow opacity.
  float shadow_range{200}, shadow_bias{.05f}, shadow_darkness{1};
  unsigned shadow_size{4096};
  // Linear RGB multipliers. Defaults reproduce the original warm-white sun
  // and neutral ambient fill exactly.
  std::array<float, 3> color{1, .96f, .86f}, ambient_color{1, 1, 1};
  // Sky dome and distance haze. Manual: MW34_SKY_TINT multiplies the sky
  // dome, MW34_HAZE_COLOR replaces the fog colour. Otherwise (auto) both are
  // derived from the sun/ambient colours and sun height; default colours
  // give an exact identity, so the original sky and fog are untouched.
  std::array<float, 3> sky_tint{1, 1, 1}, haze_scale{1, 1, 1}, haze_color{};
  bool sky_active{}, haze_active{}, haze_manual{}, sky_manual{};
  // Per-pixel sky shading (dome, horizon band and directional haze):
  // brightness, glow (colour, strength, focus), anti-sun brightness and
  // saturation, anti-sun haze brightness, horizon blend height.
  bool sky_shading{};
  bool suppress_sky{};
  float sky_source{1.f};
  float sky_brightness{1}, glow_strength{0}, glow_focus{2.5f},
      anti_brightness{1}, anti_saturation{1}, anti_haze{1}, horizon_low{.03f},
      horizon{.28f};
  std::array<float, 3> glow_color{1, 1, 1};

  // Campaign & dynamic weather parameters
  float rain_wetness{0.0f};
  float dust_intensity{0.0f};
  float heat_shimmer{0.0f};
  float lava_glow{0.0f};
  float valley_fog_density{0.0f};
  float valley_fog_altitude{0.0f};
  float night_world_gain{1.0f};
  float sdr_shadow_lift{0.f};
  float sdr_extra_shadow_lift{0.f};
};
inline Settings display_ambient(Settings settings, bool lighting_mode_enabled,
                                bool hdr_enabled) {
  if (settings.enabled && lighting_mode_enabled && !hdr_enabled) {
    settings.ambient *= 1.2f;
    settings.sdr_shadow_lift = .78f;
  }
  return settings;
}

inline void atmosphere(Settings &s) {
  const bool automatic = true;
  // Low-sun weight: 0 at 30 degrees and above, 1 at 5 degrees and below.
  const float w = std::fmin(1.f, std::fmax(0.f, (30.f - s.elevation) / 25.f));
  constexpr std::array<float, 3> neutral{1, .96f, .86f};
  std::array<float, 3> sun{}, haze{}, sky{};
  for (unsigned i = 0; i < 3; ++i)
    sun[i] = s.color[i] / neutral[i];
  const float hm = .3f + .55f * w, sm = .15f + .3f * w;
  for (unsigned i = 0; i < 3; ++i) {
    haze[i] = s.ambient_color[i] + (sun[i] - s.ambient_color[i]) * hm;
    sky[i] = s.ambient_color[i] + (sun[i] - s.ambient_color[i]) * sm;
  }
  // Haze keeps the original fog brightness (luminance-normalised tint), dimmed
  // slightly at low sun.
  const float lum = .2126f * haze[0] + .7152f * haze[1] + .0722f * haze[2];
  const bool neutralLight =
      s.color == neutral && s.ambient_color == std::array<float, 3>{1, 1, 1};
  if (automatic && !neutralLight && lum > 1e-4f) {
    for (auto &x : haze)
      x = x / lum * (1.f - .1f * w);
    s.haze_scale = haze;
    s.sky_tint = sky;
  }
  s.sky_active = s.sky_tint != std::array<float, 3>{1, 1, 1};
  s.haze_active =
      s.haze_manual || s.haze_scale != std::array<float, 3>{1, 1, 1};
  if (automatic && !neutralLight) {
    s.sky_brightness = 1.f - .2f * w;
    s.glow_strength = .55f * w;
    s.anti_brightness = 1.f - .5f * w;
    s.anti_saturation = 1.f - .6f * w;
    s.anti_haze = 1.f - .35f * w;
    const float peak = std::fmax(s.color[0], std::fmax(s.color[1], s.color[2]));
    if (peak > 0)
      for (unsigned i = 0; i < 3; ++i)
        s.glow_color[i] = s.color[i] / peak;
  }
  s.sky_shading = s.sky_active || s.haze_active || s.glow_strength > 0 ||
                  s.sky_brightness != 1 || s.anti_brightness != 1 ||
                  s.anti_haze != 1;
}

inline Settings from_config(const cc_mw3_re::RendererBridgeConfig &cfg) {
  Settings s;
  s.enabled = cfg.sun;
  s.normals = cfg.sun_normals;
  s.azimuth = cfg.sun_azimuth;
  s.elevation = cfg.sun_elevation;
  s.shadows = cfg.sun_shadows;
  if (cfg.lighting == "off" && cfg.geometry != "legacy") {
    s.strength = 0;
    s.ambient = 1;
    s.ambient_color = {1, 1, 1};
  }
  s.normal_strength = s.ground_normal_strength = s.normals ? 1.0f : 0.0f;
  s.weather_enabled = false;
  return s;
}

inline const Settings &static_settings() {
  static const Settings value = {};
  return value;
}

inline const Settings &settings() { return static_settings(); }
// Rows of inverse-transpose(local-to-world), padded with translation for std140
// vec4s.
inline std::array<float, 12> normal_matrix(const std::array<double, 16> &m) {
  for (double x : m)
    if (!std::isfinite(x))
      throw std::runtime_error("Nonfinite sun instance transform");
  const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6],
               g = m[8], h = m[9], i = m[10];
  const double det =
      a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!std::isfinite(det) || std::abs(det) < 1e-10)
    throw std::runtime_error("Singular sun instance transform");
  std::array<float, 12> out{
      float((e * i - f * h) / det), float((f * g - d * i) / det),
      float((d * h - e * g) / det), float(m[3]),
      float((c * h - b * i) / det), float((a * i - c * g) / det),
      float((b * g - a * h) / det), float(m[7]),
      float((b * f - c * e) / det), float((c * d - a * f) / det),
      float((a * e - b * d) / det), float(m[11])};
  for (float x : out)
    if (!std::isfinite(x))
      throw std::runtime_error("Sun normal transform cannot be represented");
  return out;
}
// Rows of local-to-world with translation; unlike normal_matrix this keeps
// scale.
inline std::array<float, 12> world_matrix(const std::array<double, 16> &m) {
  std::array<float, 12> out{};
  for (unsigned i = 0; i < 12; ++i) {
    out[i] = float(m[i]);
    if (!std::isfinite(out[i]))
      throw std::runtime_error("Sun world transform cannot be represented");
  }
  return out;
}
struct alignas(16) Uniform {
  std::array<float, 12> normal{};
  std::array<float, 4> direction{}, color{}, ambient{};
  std::array<float, 4> projection{}, extra{};
  // Material surface: (roughness, reflectivity, metallic, active) and eye
  // (world xyz, valid); zero values preserve the legacy lighting path.
  std::array<float, 4> surface_parameters{}, surface_eye{};
  // Shadow receivers: true local-to-world rows, world-to-sun-clip rows and
  // (active, world units per texel, depth bias in sun clip, opacity).
  std::array<float, 12> world{}, shadow{};
  std::array<float, 4> shadow_params{};
  // Sky shading: eye (xyz, w=1 active), sky tint, glow (rgb, strength),
  // grade (brightness, anti-sun brightness, anti-sun saturation, glow focus),
  // blend (horizon low, horizon high, anti-sun haze, unused).
  std::array<float, 4> sky_eye{}, sky_tint{}, sky_glow{}, sky_grade{},
      sky_blend{};
  // Optional material-specific water/cloud relief; zero means legacy path.
  std::array<float, 4> detail{}, detail_motion{};
  // Dedicated weather channels; never alias cloud masks, sky flags or water
  // motion.
  std::array<float, 4> weather{},
      weather_fog{}; // wetness, heat haze, dust, lava; density, altitude,
                     // SDR shadow lift, extra SDR shadow lift
  std::array<float, 4> emission_params{};
  std::array<std::array<float, 4>, 32> emission_regions{};
  std::array<float, 4> geometry_fog0{}, geometry_fog1{};
  // Raster tile columns, rows, SSBO word offset and active flag for this draw.
  std::array<std::uint32_t, 4> tiled_lights{};
};
static_assert(sizeof(Uniform) == 992);
static_assert(offsetof(Uniform, tiled_lights) == 976);
static_assert(offsetof(Uniform, surface_parameters) == 128);
static_assert(offsetof(Uniform, surface_eye) == 144);
struct Tl {
  std::array<float, 12> camera_world{};
  std::array<float, 6> projection{};
};
inline Uniform uniform(const std::array<float, 12> &normal, const Settings &s) {
  constexpr double radians = 3.14159265358979323846 / 180;
  const double az = s.azimuth * radians, el = s.elevation * radians;
  Uniform u;
  u.normal = normal;
  u.direction = {float(std::cos(el) * std::cos(az)), float(std::sin(el)),
                 float(std::cos(el) * std::sin(az)), s.strength};
  u.color = {s.color[0], s.color[1], s.color[2], 0};
  u.ambient = {s.ambient * s.ambient_color[0], s.ambient * s.ambient_color[1],
               s.ambient * s.ambient_color[2], 0.f};
  u.extra = {0, s.normal_strength, s.shadow_debug ? 1.f : 0.f, 0};
  u.sky_tint = {s.sky_tint[0], s.sky_tint[1], s.sky_tint[2], s.sky_source};
  u.sky_glow = {s.glow_color[0], s.glow_color[1], s.glow_color[2],
                s.glow_strength};
  u.sky_grade = {s.sky_brightness, s.anti_brightness, s.anti_saturation,
                 s.glow_focus};
  u.sky_blend = {s.horizon_low, s.horizon, s.anti_haze, 0};
  u.weather = {s.rain_wetness, s.heat_shimmer, s.dust_intensity, s.lava_glow};
  u.weather_fog = {s.valley_fog_density, s.valley_fog_altitude,
                   s.sdr_shadow_lift, s.sdr_extra_shadow_lift};
  return u;
}
// Orthographic sun view: x/y span [-range,range] around a centre snapped to
// whole shadow texels (stable as the camera moves), z covers +-4 ranges along
// the light. Rows map world positions to Vulkan clip space with w=1.
struct ShadowView {
  std::array<double, 12> rows{};
  double texel{}, depth_scale{};
};
inline ShadowView shadow_view(const std::array<double, 3> &centre,
                              const Settings &s) {
  constexpr double radians = 3.14159265358979323846 / 180;
  const double az = s.azimuth * radians, el = s.elevation * radians;
  const std::array<double, 3> f{-std::cos(el) * std::cos(az), -std::sin(el),
                                -std::cos(el) * std::sin(az)};
  // right = normalize(cross(+Y, f)); up = cross(f, right). Elevation <= 89 deg.
  std::array<double, 3> r{f[2], 0, -f[0]};
  const double rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
  r = {r[0] / rl, 0, r[2] / rl};
  const std::array<double, 3> u{f[1] * r[2] - f[2] * r[1],
                                f[2] * r[0] - f[0] * r[2],
                                f[0] * r[1] - f[1] * r[0]};
  auto dot = [](const std::array<double, 3> &a,
                const std::array<double, 3> &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  for (double x : centre)
    if (!std::isfinite(x))
      throw std::runtime_error("Sun shadow centre nonfinite");
  const double range = s.shadow_range, depth = 4 * range,
               texel = 2 * range / s.shadow_size;
  const double cx = std::floor(dot(r, centre) / texel + .5) * texel,
               cy = std::floor(dot(u, centre) / texel + .5) * texel;
  ShadowView v;
  v.texel = texel;
  v.depth_scale = 1 / (2 * depth);
  v.rows = {r[0] / range,       r[1] / range,
            r[2] / range,       -cx / range,
            u[0] / range,       u[1] / range,
            u[2] / range,       -cy / range,
            f[0] / (2 * depth), f[1] / (2 * depth),
            f[2] / (2 * depth), .5 - dot(f, centre) / (2 * depth)};
  return v;
}
// Caster rows: sun clip from model-local positions (shadow view x
// local-to-world).
inline std::array<float, 12> shadow_caster(const ShadowView &v,
                                           const std::array<float, 12> &w) {
  std::array<float, 12> out{};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 4; ++j) {
      double x = j == 3 ? v.rows[i * 4 + 3] : 0;
      for (unsigned k = 0; k < 3; ++k)
        x += v.rows[i * 4 + k] * double(w[k * 4 + j]);
      out[i * 4 + j] = float(x);
      if (!std::isfinite(out[i * 4 + j]))
        throw std::runtime_error(
            "Sun shadow caster transform cannot be represented");
    }
  return out;
}
} // namespace mw34::sun
