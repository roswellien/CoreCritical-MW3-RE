#version 450
// Direct3D 6 fixed-function pixel stage for one texture stage: colour/alpha operation and arguments,
// flat or Gouraud colour, perspective or affine texture coordinates, specular add, vertex fog (specular
// alpha) and alpha test. Unmodelled states are refused on the host before a draw reaches this shader.
layout(location=0) in vec2 uvPerspective;
layout(location=1) noperspective in vec2 uvAffine;
layout(location=2) in vec4 colorSmooth;
layout(location=3) flat in vec4 colorFlat;
layout(location=4) in vec4 specularSmooth;
layout(location=5) flat in vec4 specularFlat;
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) out vec4 result;
// mode.x flags: 1 flat, 2 perspective, 4 textured, 8 specular, 16 fog, 32 alpha test,
// 64 premultiplied sharp 2D sampling (the native HUD/canvas).
// mode.y colour op | arg1 << 8 | arg2 << 16; mode.z alpha op | arg1 << 8 | arg2 << 16;
// mode.w alpha function | reference << 8.
layout(push_constant) uniform Push { vec4 target; vec4 fogColor; vec4 factor; uvec4 mode; } draw;
vec4 diffuse, specular, texel;
vec4 argument(uint a) {
    vec4 v;
    uint s = a & 15u;
    if (s == 0u || s == 1u) v = diffuse;      // D3DTA_DIFFUSE, D3DTA_CURRENT (stage 0: diffuse)
    else if (s == 2u) v = texel;              // D3DTA_TEXTURE
    else if (s == 3u) v = draw.factor;        // D3DTA_TFACTOR
    else v = specular;                        // D3DTA_SPECULAR
    if ((a & 16u) != 0u) v = 1.0 - v;         // D3DTA_COMPLEMENT
    if ((a & 32u) != 0u) v = vec4(v.a);       // D3DTA_ALPHAREPLICATE
    return v;
}
vec4 operation(uint word) {
    uint op = word & 255u;
    vec4 a = argument((word >> 8) & 255u), b = argument((word >> 16) & 255u);
    if (op == 1u) return diffuse;                         // DISABLE: current passes through
    if (op == 2u) return a;                               // SELECTARG1
    if (op == 3u) return b;                               // SELECTARG2
    if (op == 4u) return a * b;                           // MODULATE
    if (op == 5u) return min(a * b * 2.0, 1.0);           // MODULATE2X
    if (op == 6u) return min(a * b * 4.0, 1.0);           // MODULATE4X
    if (op == 7u) return min(a + b, 1.0);                 // ADD
    if (op == 8u) return clamp(a + b - 0.5, 0.0, 1.0);    // ADDSIGNED
    if (op == 9u) return clamp((a + b - 0.5) * 2.0, 0.0, 1.0); // ADDSIGNED2X
    if (op == 10u) return clamp(a - b, 0.0, 1.0);         // SUBTRACT
    if (op == 11u) return min(a + b - a * b, 1.0);        // ADDSMOOTH
    if (op == 12u) return mix(b, a, diffuse.a);           // BLENDDIFFUSEALPHA
    if (op == 13u) return mix(b, a, texel.a);             // BLENDTEXTUREALPHA
    return mix(b, a, draw.factor.a);                      // BLENDFACTORALPHA (14)
}
void main() {
    uint flags = draw.mode.x;
    diffuse = (flags & 1u) != 0u ? colorFlat : colorSmooth;
    specular = (flags & 1u) != 0u ? specularFlat : specularSmooth;
    vec2 uv = (flags & 2u) != 0u ? uvPerspective : uvAffine;
    if ((flags & 64u) != 0u) {
        // Match SDL's sharp layer sampling at the quad's actual output density.
        // Filtering and unpremultiplication stay on the GPU, even at 4K.
        vec2 size = vec2(textureSize(image, 0));
        vec2 scale = max(vec2(1.0), 1.0 / max(fwidth(uv) * size, vec2(0.000001)));
        vec2 position = uv * size;
        vec2 base = floor(position), delta = position - base - 0.5;
        vec2 radius = 0.5 - 0.5 / scale;
        texel = texture(image, (base + (delta - clamp(delta, -radius, radius)) * scale + 0.5) / size);
        if (texel.a > 0.0) texel.rgb /= texel.a;
    } else {
        texel = (flags & 4u) != 0u ? texture(image, uv) : vec4(1.0);
    }
    vec3 rgb = operation(draw.mode.y).rgb;
    float alpha = operation(draw.mode.z).a;
    if ((flags & 8u) != 0u) rgb = min(rgb + specular.rgb, 1.0);
    if ((flags & 16u) != 0u) rgb = mix(draw.fogColor.rgb, rgb, specular.a);
    if ((flags & 32u) != 0u) {
        uint f = draw.mode.w & 255u;
        float r = float((draw.mode.w >> 8) & 255u) / 255.0;
        bool pass = f == 8u || (f == 2u && alpha < r) || (f == 3u && alpha == r) || (f == 4u && alpha <= r) ||
                    (f == 5u && alpha > r) || (f == 6u && alpha != r) || (f == 7u && alpha >= r);
        if (!pass) discard;
    }
    result = vec4(rgb, alpha);
}
