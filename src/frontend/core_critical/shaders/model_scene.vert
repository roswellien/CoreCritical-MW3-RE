#version 450
// Optional replacement geometry only. Original TL vertices keep their shader.
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=0) out vec2 uvPerspective;
layout(location=1) noperspective out vec2 uvAffine;
layout(location=2) noperspective out vec4 colorSmooth;
layout(location=3) flat out vec4 colorFlat;
layout(location=4) noperspective out vec4 specularSmooth;
layout(location=5) flat out vec4 specularFlat;

// First 64 bytes match the unmodified fragment shader's Push ABI.
// target = (guestWidth, guestHeight, nearScale, nearClip).
// factor = the batch RGB colour and original diagnostic vertex-fog factor;
// override draw arguments use TEXTURE/DIFFUSE,
// so the fragment shader never selects D3DTA_TFACTOR for a model draw.
// projection = (scaleX, scaleY, centerX, centerY).
layout(push_constant) uniform ModelPush {
    vec4 target;
    vec4 fogColor;
    vec4 factor;
    uvec4 mode;
    vec4 camera0;
    vec4 camera1;
    vec4 camera2;
    vec4 projection;
} draw;

void main() {
    vec4 p = vec4(position, 1.0);
    precise vec3 camera = vec3(dot(draw.camera0, p), dot(draw.camera1, p), dot(draw.camera2, p));
    vec3 n = vec3(dot(draw.camera0.xyz, normal), dot(draw.camera1.xyz, normal), dot(draw.camera2.xyz, normal));
    float shade = 0.35 + 0.65 * max(0.0, dot(vec3(0.3, 0.6, -0.74), n) / length(n));
    vec3 rgb = floor(clamp(draw.factor.rgb * 255.0 * shade, 0.0, 255.0)) / 255.0;
    precise float rhw = draw.target.z / camera.z;
    precise vec2 screen = camera.xy * draw.projection.xy * rhw + draw.projection.zw;
    float w = 1.0 / rhw;
    vec2 ndc = (screen + 0.5) / draw.target.xy * 2.0 - 1.0;
    gl_Position = vec4(ndc * w, rhw * w, w);
    gl_PointSize = 1.0;
    uvPerspective = uvAffine = uv;
    colorSmooth = colorFlat = vec4(rgb, 1.0);
    specularSmooth = specularFlat = vec4(0.0, 0.0, 0.0, draw.factor.a);
}
