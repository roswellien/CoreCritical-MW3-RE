#pragma once
#include <cstdint>

// The model renderer's fog settings, which a world node pushes when read.
// Original file unknown: between gmod_init.c and gmod_matl.c.
namespace zmod {

struct Fog {
  int32_t on = 0;                  // 0x0074a22c
  int32_t linear = 0;              // 0x0074a230
  float color[3] = {};             // 0x0074a234
  float near_range = 0;            // 0x0074a240
  float near_sq = 0;               // 0x0074a244
  float far_range = 0;             // 0x0074a248
  float far_sq = 0;                // 0x0074a24c
  float inv_range = 0;             // 0x0074a250  1 / (far - near)
  float alt_high = 0;              // 0x0074a254
  float alt_low = 0;               // 0x0074a258
  float inv_alt = 0;               // 0x0074a25c  1 / (high - low)
  float density = 0;               // 0x0074a260
};
Fog& fog();

void fog_set_on(int32_t on);           // 0x00526b80
void fog_set_linear(int32_t linear);   // 0x00526ce0
void fog_set_density(float density);   // 0x00526cd0
void fog_set_color(const float rgb[3]);  // 0x00526cf0
void fog_set_near(float range);        // 0x00526ba0
void fog_set_far(float range);         // 0x00526c00
void fog_set_alt_high(float alt);      // 0x00526c50
void fog_set_alt_low(float alt);       // 0x00526c90
void fog_apply();                      // 0x00526de0
// Ours: FIX (bug, Owen 2026-11-04): what the frame's colour clear fills in a mission, so that pixels no draw
// covers (a band between the far ground and the sky, seen from height) are the fog's colour; black with fog off.
void fog_frame_fill(uint8_t* r, uint8_t* g, uint8_t* b);

}  // namespace zmod
