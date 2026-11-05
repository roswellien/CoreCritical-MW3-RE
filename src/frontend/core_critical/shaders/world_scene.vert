#version 450
// Development original-mesh bridge: immutable local positions from binding0,
// original per-frame colors/fog/UV from binding1. No new lighting yet.
layout(location=0) in vec3 position;
layout(location=1) in uint diffuse;
layout(location=2) in uint specular;
layout(location=3) in vec2 uv;
layout(location=0) out vec2 uvPerspective;
layout(location=1) noperspective out vec2 uvAffine;
layout(location=2) noperspective out vec4 colorSmooth;
layout(location=3) flat out vec4 colorFlat;
layout(location=4) noperspective out vec4 specularSmooth;
layout(location=5) flat out vec4 specularFlat;
layout(push_constant) uniform WorldPush {
    vec4 target; vec4 fogColor; vec4 factor; uvec4 mode;
    vec4 camera0; vec4 camera1; vec4 camera2; vec4 projection;
} draw;
vec4 unpack(uint c) { return vec4((c>>16)&255u,(c>>8)&255u,c&255u,c>>24)/255.0; }
void main() {
    vec4 p=vec4(position,1.0);
    precise vec3 camera=vec3(dot(draw.camera0,p),dot(draw.camera1,p),dot(draw.camera2,p));
    precise float rhw=draw.target.z/camera.z;
    precise vec2 screen=camera.xy*draw.projection.xy*rhw+draw.projection.zw;
    float w=1.0/rhw;
    vec2 ndc=(screen+0.5)/draw.target.xy*2.0-1.0;
    gl_Position=vec4(ndc*w,rhw*w,w);
    gl_PointSize=1.0;
    uvPerspective=uvAffine=uv;
    colorSmooth=colorFlat=unpack(diffuse);
    specularSmooth=specularFlat=unpack(specular);
}
