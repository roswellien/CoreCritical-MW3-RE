#pragma once
#include <cstdint>

// Ground fog per model and per vertex for the Direct3D model draw (docs/render.md
// section 5, docs/drafts/fogsort.md). Reads the fog state of gmod_fog.h.
// Original file unknown: between gmod_puff.cpp and zreader.cpp.
namespace zmod {

struct FogCalc {
  static constexpr int kMax = 1216;   // 0x7463a4 up to the camera buffer at 0x7476a4
  float camera_to_world[4][3] = {};   // 0x0073fc00  only column 1 (world y) is read
  const float (*verts)[3] = nullptr;  // 0x0074639c  the model's camera-space vertices
  float factor[kMax] = {};            // 0x007463a4  fog per vertex, 0 clear .. 1 full
};
FogCalc& fog_calc();
// Read-only sample shared by native model vertices and optional visual geometry.
// The caller decides whether fog is enabled; this does not send device state.
float fog_at(const float camera_vertex[3]);

// ECX = the bounding sphere's camera-space centre, stack = its radius. The most
// fog any point of the sphere can get; the caller fogs the model above 1/255.
float fog_sphere(const float centre[3], float radius);  // 0x00555640

// ECX = vertex count. Fills factor[] from verts[]; returns 1 and sends the fog
// colour to the device when any vertex has more than 1/255, else 0.
int32_t fog_vertices(int32_t count);                    // 0x00555a70

}  // namespace zmod
