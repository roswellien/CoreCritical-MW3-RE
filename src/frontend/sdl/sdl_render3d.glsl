// The Vulkan twin of sdl_render3d.hlsl (the same maths), compiled to SPIR-V
// by glslangValidator at build time, once per stage: -DVS, -DFS_TEX or
// -DFS_FLAT. SDL's GPU sets on Vulkan: vertex uniforms set 1; fragment
// sampled textures (combined image-samplers) set 2, uniforms set 3.
#version 450

#ifdef VS
layout(location = 0) in vec4 xyzrhw;
layout(location = 1) in vec4 diffuse_in;
layout(location = 2) in vec4 specular_in;
layout(location = 3) in vec2 uv_in;
layout(location = 0) out vec4 diffuse;
layout(location = 1) out vec4 specular;
layout(location = 2) out vec2 uv;
layout(set = 1, binding = 0) uniform V { vec4 viewport; };
void main() {
  float w = xyzrhw.w > 0.0 ? 1.0 / xyzrhw.w : 1.0;
  float x = (xyzrhw.x - viewport.x) / viewport.z * 2.0 - 1.0;
  float y = 1.0 - (xyzrhw.y - viewport.y) / viewport.w * 2.0;
  gl_Position = vec4(x * w, y * w, xyzrhw.z * w, w);
  diffuse = diffuse_in.zyxw;  // ARGB in memory is B, G, R, A
  specular = specular_in.zyxw;
  uv = uv_in;
}
#else
layout(location = 0) in vec4 diffuse;
layout(location = 1) in vec4 specular;
layout(location = 2) in vec2 uv;
layout(location = 0) out vec4 colour;
// opts.x fog on, opts.y alpha modulate, opts.z sharp sampling enabled
layout(set = 3, binding = 0) uniform F { vec4 fog; vec4 opts; };
vec4 finish(vec4 c) {
  if (opts.x > 0.5) c.rgb = mix(fog.rgb, c.rgb, specular.a);
  return c;
}
#ifdef FS_TEX
layout(set = 2, binding = 0) uniform sampler2D t;
vec4 sample_sharp(vec2 at) {
  vec2 size = vec2(textureSize(t, 0));
  vec2 scale = max(vec2(1.0), 1.0 / max(fwidth(at) * size, vec2(0.000001)));
  vec2 texel = at * size, base = floor(texel), d = texel - base - 0.5;
  vec2 r = 0.5 - 0.5 / scale;
  vec4 c = texture(t, (base + (d - clamp(d, -r, r)) * scale + 0.5) / size);
  return c.a > 0.0 ? vec4(c.rgb / c.a, c.a) : c;
}
void main() {
  vec4 tex = opts.z > 0.0 ? sample_sharp(uv) : texture(t, uv);
  colour = finish(vec4(tex.rgb * diffuse.rgb, opts.y > 0.5 ? tex.a * diffuse.a : tex.a));
}
#else
void main() { colour = finish(diffuse); }
#endif
#endif
