#version 450
// Sun shadow depth pass: replacement-model positions to orthographic sun clip.
layout(location=0) in vec3 position;
layout(push_constant) uniform ShadowPush { vec4 row0; vec4 row1; vec4 row2; } caster;
void main() {
    vec4 p=vec4(position,1.0);
    gl_Position=vec4(dot(caster.row0,p),dot(caster.row1,p),dot(caster.row2,p),1.0);
}
