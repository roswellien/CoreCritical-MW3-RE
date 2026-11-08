#version 450
// Development original-mesh bridge: immutable local positions from binding0,
// original alpha/fog/UV from binding1. Experimental per-pixel world sun.
layout(location=0) in vec3 position;
layout(location=1) in uint diffuse;
layout(location=2) in uint specular;
layout(location=3) in vec2 uv;
layout(location=4) in vec3 normal;
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

layout(location=0) out vec2 uvPerspective;
layout(location=1) noperspective out vec2 uvAffine;
layout(location=2) out vec4 colorSmooth;
layout(location=3) flat out vec4 colorFlat;
layout(location=4) out vec4 specularSmooth;
layout(location=5) flat out vec4 specularFlat;
layout(push_constant) uniform WorldPush {
    vec4 target; vec4 fogColor; vec4 factor; uvec4 mode;
    vec4 camera0; vec4 camera1; vec4 camera2; vec4 projection;
} draw;
vec4 unpack(uint c) { return vec4((c>>16)&255u,(c>>8)&255u,c&255u,c>>24)/255.0; }
void main() {
    vec4 p=vec4(position,1.0);
    worldPosition=vec3(dot(sun.world0,p),dot(sun.world1,p),dot(sun.world2,p));
    shadowPosition=worldPosition;
    precise vec3 camera=vec3(dot(draw.camera0,p),dot(draw.camera1,p),dot(draw.camera2,p));
    precise float rhw=draw.target.z/camera.z;
    precise vec2 screen=camera.xy*draw.projection.xy*rhw+draw.projection.zw;
    float w=1.0/rhw;
    vec2 ndc=(screen+0.5)/draw.target.xy*2.0-1.0;
    gl_Position=vec4(ndc*w,rhw*w,w);
    screenPos=screen;
    gl_PointSize=1.0;
    uvPerspective=uvAffine=uv;
    // Neutral texture albedo replaces the original CPU-lit RGB in this test.
    // Retain alpha and the original fog coefficient; painted texture shading remains.
    colorSmooth=colorFlat=vec4(1.0,1.0,1.0,unpack(diffuse).a);
    worldNormal=vec3(dot(sun.normal0.xyz,normal),dot(sun.normal1.xyz,normal),dot(sun.normal2.xyz,normal));
    specularSmooth=specularFlat=vec4(0.0,0.0,0.0,unpack(specular).a);
}
