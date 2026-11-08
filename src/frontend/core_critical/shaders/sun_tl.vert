#version 450
// Direct3D 6 transformed-and-lit vertex (D3DFVF_TLVERTEX, 32 bytes): screen x,y, z, rhw, diffuse, specular, u,v.
layout(location=0) in vec4 position;
layout(location=1) in uint diffuse;
layout(location=2) in uint specular;
layout(location=3) in vec2 uv;
layout(location=4) in vec3 smoothNormal;
layout(location=0) out vec2 uvPerspective;
layout(location=1) noperspective out vec2 uvAffine;
layout(location=2) out vec4 colorSmooth;
layout(location=3) flat out vec4 colorFlat;
layout(location=4) out vec4 specularSmooth;
layout(location=5) flat out vec4 specularFlat;
layout(location=6) out vec3 worldNormal;
layout(location=7) out vec3 worldPosition;
layout(location=8) out vec3 shadowPosition;
layout(location=9) noperspective out vec2 screenPos;
layout(set=1,binding=0,std140) uniform SunLighting {
 vec4 normal0; vec4 normal1; vec4 normal2;
 vec4 direction; vec4 sunColor; vec4 ambient;
 vec4 projection; vec4 extra;
 vec4 surfaceParameters; vec4 surfaceEye;
 vec4 world0; vec4 world1; vec4 world2;
 vec4 shadow0; vec4 shadow1; vec4 shadow2; vec4 shadowParams;
 vec4 skyEye; vec4 skyTint; vec4 skyGlow; vec4 skyGrade; vec4 skyBlend;
 vec4 detail; vec4 detailMotion;
} sun;

layout(push_constant) uniform Push { vec4 target; vec4 fogColor; vec4 factor; uvec4 mode; } draw;
vec4 unpack(uint c) { return vec4((c >> 16) & 255u, (c >> 8) & 255u, c & 255u, c >> 24) / 255.0; }
void main() {
    // Direct3D pixel centres lie on integer coordinates; Vulkan's lie on half-integers (target.z = 0.5).
    float w = 1.0 / position.w;
    vec2 ndc = (position.xy + draw.target.z) / draw.target.xy * 2.0 - 1.0;
    gl_Position = vec4(ndc * w, position.z * w, w);
    gl_PointSize = 1.0;  // D3DPT_POINTLIST: one pixel (ignored for triangles)
    uvPerspective = uv;
    uvAffine = uv;
    // Sky dome/horizon (extra.w >= 2) keep their original vertex colour; lit receivers use neutral albedo.
    colorSmooth=colorFlat=(sun.extra.w>1.5||(draw.mode.x&4u)==0u)?unpack(diffuse):vec4(1.0,1.0,1.0,unpack(diffuse).a);
    vec3 camera=vec3((position.x-sun.projection.z)/(sun.projection.x*position.z),
                     (position.y-sun.projection.w)/(sun.projection.y*position.z),sun.extra.x/position.z);
    vec4 p=vec4(camera,1.0);
    worldPosition=vec3(dot(sun.normal0,p),dot(sun.normal1,p),dot(sun.normal2,p));
    shadowPosition=worldPosition;
    screenPos=position.xy;
    // Native cameras are rigid. The adapter has already applied the model's
    // inverse-transpose before clipping these camera-space normals.
    worldNormal=vec3(dot(sun.normal0.xyz,smoothNormal),dot(sun.normal1.xyz,smoothNormal),dot(sun.normal2.xyz,smoothNormal));
    specularSmooth=specularFlat=vec4(0.0,0.0,0.0,unpack(specular).a);
}
