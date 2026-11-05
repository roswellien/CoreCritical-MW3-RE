// The Direct3D 12 twin of sdl_render3d.cpp's Metal shaders (the same
// maths), compiled to DXIL by dxc at build time. SDL's GPU registers:
// vertex uniforms b0 space1; fragment textures and samplers space2,
// uniforms b0 space3; vertex attribute N is TEXCOORDN.
struct In {
  float4 xyzrhw : TEXCOORD0;
  float4 diffuse : TEXCOORD1;
  float4 specular : TEXCOORD2;
  float2 uv : TEXCOORD3;
};
struct Out {
  float4 pos : SV_Position;
  float4 diffuse : TEXCOORD0;
  float4 specular : TEXCOORD1;
  float2 uv : TEXCOORD2;
};
cbuffer V : register(b0, space1) { float4 viewport; };
Out vs(In i) {
  Out o;
  float w = i.xyzrhw.w > 0.0 ? 1.0 / i.xyzrhw.w : 1.0;
  float x = (i.xyzrhw.x - viewport.x) / viewport.z * 2.0 - 1.0;
  float y = 1.0 - (i.xyzrhw.y - viewport.y) / viewport.w * 2.0;
  o.pos = float4(x * w, y * w, i.xyzrhw.z * w, w);
  o.diffuse = i.diffuse.zyxw;  // ARGB in memory is B, G, R, A
  o.specular = i.specular.zyxw;
  o.uv = i.uv;
  return o;
}
// opts.x fog on, opts.y alpha modulate, opts.z sharp sampling enabled
cbuffer F : register(b0, space3) { float4 fog; float4 opts; };
Texture2D<float4> t : register(t0, space2);
SamplerState s : register(s0, space2);
float4 finish(float4 c, Out i) {
  if (opts.x > 0.5) c.rgb = lerp(fog.rgb, c.rgb, i.specular.a);
  return c;
}
float4 sample_sharp(float2 uv) {
  float2 size;
  t.GetDimensions(size.x, size.y);
  float2 scale = max(float2(1.0, 1.0), 1.0 / max(fwidth(uv) * size, float2(0.000001, 0.000001)));
  float2 texel = uv * size, base = floor(texel), d = texel - base - 0.5;
  float2 r = 0.5 - 0.5 / scale;
  float4 c = t.Sample(s, (base + (d - clamp(d, -r, r)) * scale + 0.5) / size);
  return c.a > 0.0 ? float4(c.rgb / c.a, c.a) : c;
}
float4 fs_tex(Out i) : SV_Target0 {
  float4 tex = opts.z > 0.0 ? sample_sharp(i.uv) : t.Sample(s, i.uv);
  float4 c = float4(tex.rgb * i.diffuse.rgb, opts.y > 0.5 ? tex.a * i.diffuse.a : tex.a);
  return finish(c, i);
}
float4 fs_flat(Out i) : SV_Target0 { return finish(i.diffuse, i); }
