#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <memory>

namespace cc::geometry { struct Resource; }

#include "gamez/zmodel/gmod_matl.h"

// The world model pool (zModel\gmod_const.c) that gamez.zbd fills (docs/gamez.md).
namespace zmod {

struct Light {                   // 0x4c bytes
  uint8_t raw[0x4c];             // as read; colour floats at +0x1c/+0x20/+0x24
  uint16_t color;                // +0x28  display-packed
  float* vertices;               // +0x2c  count at +0x0c
};

struct Polygon {                 // 0x24 bytes
  uint32_t flags;                // +0x00  bits 0-7 vertex count, 0x200 normals
  int32_t priority;              // +0x04
  uint32_t* vertex_indices;      // +0x08
  uint32_t* normal_indices;      // +0x0c
  float* uvs;                    // +0x10  when the material is textured
  float* colors;                 // +0x14  always
  uint32_t* scratch;             // +0x18
  Material* material;            // +0x1c  an index in the file
  uint32_t zone_set;             // +0x20
  // A mechlib polygon's +0x1c until the library resolves it (0x004fcd40):
  // the index in mechlib's own material list.
  uint32_t material_index = 0;
};

struct Model {                   // 0x60-byte slot
  uint8_t header[0x5c];          // as read; counts at +0x0c..+0x20
  uint32_t link;                 // +0x5c  free link, or the data's file offset
  Polygon* polygons = nullptr;   // +0x34
  float* vertices = nullptr;     // +0x38
  float* normals = nullptr;      // +0x3c
  Light* lights = nullptr;       // +0x40
  float* morphs = nullptr;       // +0x44
  // Owned rendering snapshot; invalidated by every source geometry mutator.
  // These host fields are not part of the serialized 0x60-byte model slot.
  std::shared_ptr<const cc::geometry::Resource> render_geometry;
  std::vector<const Material*> render_materials;
  void invalidate_render_geometry() noexcept {
    render_geometry.reset();
    render_materials.clear();
  }
  uint32_t count(int off) const;
};

struct ModelPool {
  std::vector<Model> slots;      // 0x007402e4
  int32_t size = 0;              // 0x007402e0
  int32_t count = 0;             // 0x007402e8
  int32_t free_head = -1;        // 0x007402ec
};
ModelPool& models();

Model* model_at(int32_t index);               // 0x005380c0
// zModel's start (0x00526800's pool part): `size` slots ([0x007402e0], 1750
// when unset), all free. A world's gamez.zbd then replaces the pool.
void reset_pool();
// A free slot, its header cleared and +8 = 3 (flag 0x10 off). An empty pool
// stops the game ("GameZ model resource failure").
Model* model_alloc();                         // 0x00538fe0
int read_models(std::FILE* f);                // 0x00538e30
int read_model_data(std::FILE* f, Model& m);  // 0x00538a70
int model_release(Model* m);                  // 0x00539150, 1 while used (+0xc)
int32_t models_free_unused();                 // 0x005392c0: the models nothing holds released; their count
void models_purge(bool models, bool materials);  // 0x005394c0 ECX, EDX
Model* reread_model(std::FILE* f, int32_t index);  // 0x00538640, f at the models' table
// A new pool model with m's arrays copied (its use count 0, its sphere at
// +0x48 not). With `own_materials` each polygon's material is copied too
// (one copy per material of m), unless `shared` and the material does not
// change (0x00536ab0); without, they are m's.
Model* model_copy(const Model& m, int32_t own_materials, int32_t shared);  // 0x005396c0 ECX m, EDX, stack
// Flags 4, 8 or 0x10, or a material that changes: a model a copy may not share.
int model_changes(const Model& m);            // 0x0053b3c0

// The texture cycle of the model's first polygon's material (gmod_const.c):
// back to its first texture, or to frame n (past the end it wraps, below 0
// it is 0). No cycle: nothing.
void cycle_reset(Model* m);                   // 0x0053bc10
void cycle_set_frame(Model* m, int32_t n);    // 0x0053bc90
// The interp's CycleTexture* commands on a model (docs/cockpit-damage.md):
// each works on the first polygon's material; a null model does nothing.
int cycle_set_on(Model* m, int32_t count);         // 0x0053bc30: count slots (cycle_create)
int cycle_set_looping(Model* m, uint32_t on);      // 0x0053bcd0
int cycle_set_speed(Model* m, float speed);        // 0x0053bcf0
int cycle_set_map(Model* m, zimg::TextureEntry* t);  // 0x0053bd10: the next slot (cycle_add)

// A model's box (min xyz, max xyz) over its vertices, its lights' points
// and its fully morphed vertices, then its bounding centre (+0x48) and
// radius (+0x54, a square root by halving the float's bits). A facade
// (type 1) turns the box into one centred on the model's origin. `header` is
// the model's 0x5c bytes; mechlib models (zcls::Model) have no lights.
// Box untouched when the model has neither vertices nor lights.
void model_box(uint8_t* header, const float* vertices, const Light* lights, const float* morphs,
               float box[6]);              // 0x0053b4e0 ECX model, EDX box (0x0053b680 the extent)
inline void model_box(Model* m, float box[6]) { model_box(m->header, m->vertices, m->lights, m->morphs, box); }

// Adding a polygon to a model (gmod_const.c, the crater's polygons;
// docs/craters.md section 5). The arguments 0x0053c6c0 takes on the stack.
struct PolygonAdd {
  float* points = nullptr;              // the caller's: colinear points are dropped in place
  const float* normals = nullptr;       // optional (polygon flag 0x200)
  const float* uvs = nullptr;           // 2 floats a point, read when the material is textured
  int32_t morph = 0;                    // with morph points (0x00539ec0)
  const float* morph_points = nullptr;
  const float* morph_uvs = nullptr;
  Material* material = nullptr;         // its flag 0x100: textured
  int32_t priority = 0;
  uint32_t flag = 0;                    // bit 0: polygon flag 0x100
  const uint8_t* zone = nullptr;        // 4 bytes, the polygon's +0x20
  const float* colors = nullptr;        // 3 floats a point; null: white
};
// The vertex merge distance (0x005ee3e0, 0.001; a crater sets 0.005 while
// it is made).
float vertex_precision();                      // 0x00538050
// The planarity (0x005ee3d0) and colinear (0x005ee3d8) tolerances, doubles
// in the exe; the shipped scripts set 0.002 and 0.00001 (tolerances.gw).
void set_coplanar_tolerance(float v);          // 0x00538070
void set_colinear_tolerance(float v);          // 0x00538080
void tolerances_reset();                       // tests: the exe's data (0.005, 0.001)
void set_vertex_precision(float v);            // 0x00538060
// 0 added, 1 refused (fewer than 3 points, more than 57, fewer than 3 once
// colinear points go, or the model full), 2 added as a fan of triangles
// (not planar to 0.005) or in pieces (more than 48 points).
int model_add_polygon(Model* m, int32_t n, const PolygonAdd& a);  // 0x0053c6c0 ECX m, EDX n
// Every polygon's priority (+0x04) set; none on a null model. The exe returns
// what EAX holds; callers ignore it (0 here, for the script's engine table).
int model_set_priority(Model* m, int32_t priority);  // 0x0053bb20 ECX m, EDX priority
// `kept` (optional) loses the index of each point dropped.
int drop_colinear(int32_t* n, float* points, std::vector<int32_t>* kept = nullptr);  // 0x0053a530 ECX n, EDX points
int32_t find_vertex(Model& m, const float p[3]);                     // 0x00539d80
int32_t find_morph_vertex(Model& m, const float p[3], const float to[3]);  // 0x00539ec0
int32_t find_normal(Model& m, const float p[3]);                     // 0x0053a070
int add_record(Model& m, int32_t n, const PolygonAdd& a);           // 0x0053c370
int fill_record(Model& m, int32_t n, const PolygonAdd& a);          // 0x0053c500
void flatten_uvs(Model& m, int32_t index);                           // 0x0053bdc0
// The texture scroll's step, once a frame (+0x30): the textured polygons'
// UVs moved by dt times +0x28 / +0x2c (set by gmod_init's model_set_scroll).
int model_scroll_tick(Model* m);                                     // 0x00541ad0, -1 for none
void uv_gradient(float a0, float b0, float a1, float b1, float a2, float b2, float s0, float s1, float s2,
                 float out[2]);                                      // 0x0053c270
void snap_uvs(int32_t n, float* uv);                                 // 0x0053af50

// Strip polygons (flag 0x400) into triangles, on a model whose +0 is 0.
void expand_strips(Model& m);                                        // 0x00553160 ECX m

}  // namespace zmod
