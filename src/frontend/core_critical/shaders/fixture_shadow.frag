#version 450
layout(location=0) in vec2 textureUV;
layout(set=0,binding=0) uniform sampler2D colourTexture;
layout(push_constant) uniform FixtureShadowPush {
    vec4 row0; vec4 row1; vec4 row2; vec4 row3;
    vec4 alphaTest;
} caster;
void main() {
    if(caster.alphaTest.z<0.5)return;
    int value=int(round(texture(colourTexture,textureUV).a*255.0));
    int reference=int(caster.alphaTest.y),function=int(caster.alphaTest.x);
    bool keep=function==8 || (function==2 && value<reference) ||
        (function==3 && value==reference) || (function==4 && value<=reference) ||
        (function==5 && value>reference) || (function==6 && value!=reference) ||
        (function==7 && value>=reference);
    if(!keep)discard;
}
