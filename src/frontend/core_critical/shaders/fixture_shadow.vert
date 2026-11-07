#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=0) out vec2 textureUV;
layout(push_constant) uniform FixtureShadowPush {
    vec4 row0; vec4 row1; vec4 row2; vec4 row3;
    vec4 alphaTest;
} caster;
void main() {
    vec4 p=vec4(position,1.0);
    gl_Position=vec4(dot(caster.row0,p),dot(caster.row1,p),dot(caster.row2,p),dot(caster.row3,p));
    textureUV=uv;
}
