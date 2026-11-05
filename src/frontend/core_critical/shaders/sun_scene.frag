#version 450
// Direct3D 6 fixed-function pixel stage for one texture stage: colour/alpha operation and arguments,
// flat or Gouraud colour, perspective or affine texture coordinates, specular add, vertex fog (specular
// alpha) and alpha test. Unmodelled states are refused on the host before a draw reaches this shader.
layout(location=0) in vec2 uvPerspective;
layout(location=1) noperspective in vec2 uvAffine;
layout(location=2) noperspective in vec4 colorSmooth;
layout(location=3) flat in vec4 colorFlat;
layout(location=4) noperspective in vec4 specularSmooth;
layout(location=5) flat in vec4 specularFlat;
layout(location=6) in vec3 worldNormal;
layout(location=7) in vec3 worldPosition;
layout(location=8) in vec3 shadowPosition;
layout(location=9) noperspective in vec2 screenPos;
layout(set=1,binding=0,std140) uniform SunLighting {
 vec4 normal0; vec4 normal1; vec4 normal2;
 vec4 direction; vec4 sunColor; vec4 ambient;
 vec4 projection; vec4 extra;
 vec4 world0; vec4 world1; vec4 world2;
 vec4 shadow0; vec4 shadow1; vec4 shadow2; vec4 shadowParams;
 vec4 skyEye; vec4 skyTint; vec4 skyGlow; vec4 skyGrade; vec4 skyBlend;
 vec4 detail; vec4 detailMotion;
 vec4 weather; vec4 weatherFog;
 vec4 emissionParams; vec4 emissionRegions[32];
} sun;
#ifdef MW34_EFFECT_LIGHTING
struct EffectLight {vec4 start;vec4 end;vec4 color;};
layout(set=1,binding=2,std140) uniform EffectLighting {
 vec4 params;
 EffectLight lights[64];
 vec4 shadowMeta[64];
 vec4 cones[64];
} effects;
layout(set=1,binding=3) uniform sampler2D fixtureShadowAtlas;
float fixtureVisibility(int index,vec3 p,vec3 n){
 vec4 meta=effects.shadowMeta[index];if(meta.x<0.5)return 1.0;
 vec3 d=p+n*meta.w-effects.lights[index].start.xyz;
 vec3 magnitude=abs(d),facePosition;int face;
 if(magnitude.x>=magnitude.y&&magnitude.x>=magnitude.z){
  if(d.x>=0.0){face=0;facePosition=vec3(-d.z,-d.y,d.x);}
  else{face=1;facePosition=vec3(d.z,-d.y,-d.x);}
 }else if(magnitude.y>=magnitude.z){
  if(d.y>=0.0){face=2;facePosition=vec3(d.x,d.z,d.y);}
  else{face=3;facePosition=vec3(d.x,-d.z,-d.y);}
 }else{
  if(d.z>=0.0){face=4;facePosition=vec3(d.x,-d.y,d.z);}
  else{face=5;facePosition=vec3(-d.x,-d.y,-d.z);}
 }
 float nearPlane=meta.y,farPlane=meta.z;
 if(facePosition.z<=nearPlane||facePosition.z>=farPlane)return 1.0;
 float reference=farPlane/(farPlane-nearPlane)-farPlane*nearPlane/((farPlane-nearPlane)*facePosition.z)-0.00005;
 int tile=index*6+face;ivec2 atlas=textureSize(fixtureShadowAtlas,0);
 vec2 size=vec2(atlas)/vec2(16,24),origin=vec2(tile%16,tile/16)*size;
 vec2 local=(facePosition.xy/facePosition.z*.5+.5)*size;
 float visibility=0.0;
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  vec2 pixel=origin+clamp(local+vec2(x,y)-.5,vec2(.5),size-.5);
  visibility+=reference<=texture(fixtureShadowAtlas,pixel/vec2(atlas)).r?1.0:0.0;
 }
 return visibility*.25;
}

vec3 effectIllumination(vec3 p,vec3 n){
 vec3 illumination=vec3(0);
 for(int i=0;i<min(int(effects.params.x),64);++i){
  EffectLight l=effects.lights[i];
  vec3 delta;
  float spotFactor=1.0;
  if(l.end.w>1.5){ // 2 = spotlight; 1 remains a laser capsule endpoint.
   delta=l.start.xyz-p;
   vec3 dirToSurface=-delta*inversesqrt(max(dot(delta,delta),1e-8));
   float cosAngle=dot(dirToSurface,l.end.xyz);
   if(effects.cones[i].z>0.5){
    // Tiny authored angles may have equal binary32 cosines: use their hard edge.
    float inner=effects.cones[i].x,outer=effects.cones[i].y;
    spotFactor=inner>outer?smoothstep(outer,inner,cosAngle):step(outer,cosAngle);
   }else spotFactor=clamp((cosAngle-0.2)*2.0,0.0,1.0);
  }else{
   vec3 segment=l.end.xyz-l.start.xyz;
   float den=dot(segment,segment);
   float t=den>1e-8?clamp(dot(p-l.start.xyz,segment)/den,0.0,1.0):0.0;
   delta=l.start.xyz+t*segment-p;
  }
  float d2=dot(delta,delta),r2=l.start.w*l.start.w;
  if(d2>=r2||spotFactor<=0.0)continue;
  float attenuation=1.0-d2/r2;
  float diffuse=max(dot(n,delta*inversesqrt(max(d2,1e-8))),0.0);
  illumination+=l.color.rgb*l.color.w*attenuation*attenuation*diffuse*spotFactor*fixtureVisibility(i,p,n);
 }
 return illumination;
}
#endif

layout(set=0,binding=0) uniform sampler2D image;
layout(set=1,binding=1) uniform sampler2DShadow shadowMap;
layout(set=2,binding=0) uniform sampler2D normalMap;
layout(location=0) out vec4 result;
// mode.x flags: 1 flat, 2 perspective, 4 textured, 8 specular, 16 fog, 32 alpha test.
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
// Sun shadow map (replacement mechs only). Receivers offset along the
// geometric normal, then take nine bilinear depth comparisons (a 4x4 texel
// footprint). The edge of the sun-space region fades back to full light.
float sunShadow(vec3 N) {
    float ndl=clamp(dot(N,sun.direction.xyz),0.0,1.0);
    vec4 q=vec4(shadowPosition+N*(sun.shadowParams.y*(1.5+2.0*(1.0-ndl))),1.0);
    vec3 s=vec3(dot(sun.shadow0,q),dot(sun.shadow1,q),dot(sun.shadow2,q));
    vec2 uv=s.xy*0.5+0.5;
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    if(edge<=0.0||s.z>=1.0)return 1.0;
    vec2 texel=1.0/vec2(textureSize(shadowMap,0));
    float ref=s.z-sun.shadowParams.z,visible=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)visible+=texture(shadowMap,vec3(uv+vec2(x,y)*texel,ref));
    return mix(1.0,visible/9.0,sun.shadowParams.w*smoothstep(0.0,0.05,edge));
}
// Sky shading. View direction from the camera to the shaded point; "side" is
// 1 toward the sun's azimuth and 0 opposite. The glow concentrates around the
// sun and spreads along the sun-side horizon.
vec3 skyView() { return normalize(shadowPosition - sun.skyEye.xyz); }
// Exact per-pixel view ray for the TL sky dome/band (their far-plane z and rhw
// disagree, so interpolated positions bend per triangle).
vec3 skyRay() {
    vec3 c=vec3((screenPos.x-sun.projection.z)/sun.projection.x,(screenPos.y-sun.projection.w)/sun.projection.y,sun.extra.x);
    return normalize(vec3(dot(sun.normal0.xyz,c),dot(sun.normal1.xyz,c),dot(sun.normal2.xyz,c)));
}
float skySide(vec3 v) {
    vec2 a=v.xz,b=sun.direction.xz;float la=length(a),lb=length(b);
    return (la<1e-5||lb<1e-5)?0.5:clamp(0.5+0.5*dot(a/la,b/lb),0.0,1.0);
}
float skyGlowAmount(vec3 v,float side) {
    float angle=acos(clamp(dot(v,sun.direction.xyz),-1.0,1.0));
    return sun.skyGlow.a*(exp(-angle*sun.skyGrade.w)+0.35*side*side*side*exp(-max(v.y,0.0)*4.0));
}
// The scene target may be RGB565: dither smooth sky/haze gradients by about
// one 5-bit step so they do not band into visible contours.
vec3 skyDither(vec3 c) {
    float n=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))));
    return clamp(c+(n-0.5)/31.0,0.0,1.0);
}
vec3 hazeColor(vec3 v) {
    float side=skySide(v);
    return draw.fogColor.rgb*mix(sun.skyBlend.z,1.0,side)+sun.skyGlow.rgb*skyGlowAmount(v,side)*0.6;
}
// Cloud relief changes the existing cloud artwork's response around unity.
// The dome is never lit as a solid surface; a flat map preserves its color.
float cloudRelief(vec2 uv,vec3 ray) {
    vec4 map=texture(normalMap,uv);
    vec2 slope=(map.xy*255.0-128.0)/127.0;
    vec3 N=-ray;
    vec3 dp1=dFdx(ray),dp2=dFdy(ray);
    vec2 du1=dFdx(uv),du2=dFdy(uv);
    vec3 p2=cross(dp2,N),p1=cross(N,dp1);
    vec3 T=p2*du1.x+p1*du2.x,B=p2*du1.y+p1*du2.y;
    float d=max(dot(T,T),dot(B,B));
    if(d<=1e-30)return 1.0;
    vec3 change=(T*slope.x+B*slope.y)*inversesqrt(d);
    return clamp(1.0+dot(change,sun.direction.xyz)*sun.detail.y*map.a*.8,.7,1.3);
}
// Shared world XZ field makes water and the water side of surf follow one
// wave phase across texture boundaries. Normal alpha masks foam/sand at UV.
float surfMask(vec2 uv) {
    // Ripple RGB repeats, but atlas masks must clamp: at U=1, repeat would
    // blend the water edge back into sand from the other side of the atlas.
    ivec2 size=textureSize(normalMap,0);
    vec2 p=clamp(uv,0.0,1.0)*vec2(size)-.5;
    ivec2 a=ivec2(floor(p));vec2 f=fract(p);
    // Explicit clamped taps bypass the ripple sampler's repeating/anisotropic
    // footprint and its mip chain, including at exactly U/V=0 or 1.
    float aa=texelFetch(normalMap,clamp(a,ivec2(0),size-1),0).a;
    float ba=texelFetch(normalMap,clamp(a+ivec2(1,0),ivec2(0),size-1),0).a;
    float ab=texelFetch(normalMap,clamp(a+ivec2(0,1),ivec2(0),size-1),0).a;
    float bb=texelFetch(normalMap,clamp(a+ivec2(1,1),ivec2(0),size-1),0).a;
    return mix(mix(aa,ba,f.x),mix(ab,bb,f.x),f.y);
}
vec3 waterNormal(vec3 N,float mask) {
    vec2 p=worldPosition.xz*sun.detail.z;
    float time=sun.detailMotion.x;
    vec3 a=texture(normalMap,p+time*vec2(.12,.08)).xyz;
    vec3 b=texture(normalMap,p*2.0+time*vec2(-.06,.10)).xyz;
    vec2 slope=((a.xy*255.0-128.0)+.45*(b.xy*255.0-128.0))/127.0;
    // Fade small ripples below a pixel rather than shimmering at the horizon.
    float footprint=max(length(dFdx(p)),length(dFdy(p)))*float(textureSize(normalMap,0).x);
    float fade=1.0-smoothstep(2.0,10.0,footprint);
    return normalize(N+vec3(slope.x,0,slope.y)*(sun.detail.y*mask*fade));
}
void main() {
    uint flags = draw.mode.x;
    diffuse = (flags & 1u) != 0u ? colorFlat : colorSmooth;
    specular = (flags & 1u) != 0u ? specularFlat : specularSmooth;
    vec2 uv = (flags & 2u) != 0u ? uvPerspective : uvAffine;
    texel = (flags & 4u) != 0u ? texture(image, uv) : vec4(1.0);
    if(sun.skyBlend.w>2.99){ // Path debug: green per-frame TL terrain, blue GPU-kept world, magenta mech, yellow sky/haze-only.
        float k=sun.skyBlend.w-3.0;vec3 c=k<0.5?vec3(0.1,0.85,0.1):k<1.5?vec3(0.1,0.3,1.0):k<2.5?vec3(1.0,0.1,1.0):vec3(1.0,0.9,0.1);
        float lum=dot(operation(draw.mode.y).rgb,vec3(0.333));result=vec4(c*(0.55+0.6*lum),1.0);return;
    }
    if(sun.skyBlend.w>0.5&&sun.skyBlend.w<1.99){ // Sky debug: R dome, B horizon band, G haze-only, L lit; brightness = draw index.
        float k=fract(sun.skyBlend.w);
        result=vec4(sun.extra.w>1.5&&sun.extra.w<2.5?0.3+0.7*k:0.0,sun.extra.w>3.5?0.3+0.7*k:(sun.extra.w<1.5?0.15:0.0),sun.extra.w>2.5&&sun.extra.w<3.5?0.3+0.7*k:0.0,1.0);return;
    }
    if(sun.extra.w>1.5&&sun.extra.w<3.5){ // Sky dome (2) or horizon band (3).
        // Sealed maps have no natural sky. This also avoids evaluating
        // the valid interior horizon interval [0,0] through smoothstep.
        if(sun.skyTint.w>4.5&&sun.skyTint.w<5.5){
            result=vec4(0.0,0.0,0.0,operation(draw.mode.z).a);return;
        }
        vec3 v=skyRay();float side=skySide(v);vec3 haze=hazeColor(v),c=haze;
        if(sun.extra.w<2.5){
            vec3 base;
            if(sun.skyTint.w>5.5){
                // An opaque particulate canopy, not water-bearing storm clouds.
                float height=pow(clamp(v.y,0.0,1.0),0.45);
                base=mix(haze, sun.skyTint.rgb*sun.skyGrade.x*0.55, height);
            }else if(sun.skyTint.w>1.5){
                float height=pow(clamp(v.y,0.0,1.0),0.45);
                if(sun.skyTint.w>3.5)
                    base=mix(vec3(0.12,0.16,0.26),vec3(0.015,0.025,0.055),height);
                else if(sun.skyTint.w>2.5)
                    base=mix(vec3(0.36,0.42,0.60),vec3(0.13,0.18,0.33),height);
                else
                    base=mix(vec3(0.58,0.74,0.91),vec3(0.18,0.43,0.72),height);
                base*=sun.skyTint.rgb*sun.skyGrade.x*mix(sun.skyGrade.y,1.0,pow(side,1.5));
            }else{
                vec3 legacy=operation(draw.mode.y).rgb*sun.skyTint.rgb*sun.skyGrade.x*mix(sun.skyGrade.y,1.0,pow(side,1.5));
                if(sun.detail.x>2.5)legacy*=cloudRelief(uv,v);
                base=legacy;
            }
            float l=dot(base,vec3(0.2126,0.7152,0.0722));
            base=mix(vec3(l),base,mix(sun.skyGrade.z,1.0,side))+sun.skyGlow.rgb*skyGlowAmount(v,side);
            // Elevation-only blend into the haze. Below horizon_low the dome equals the haze exactly,
            // matching the horizon band it overlaps at the same far depth.
            float f=smoothstep(sun.skyBlend.x,sun.skyBlend.y,v.y);
            c=mix(haze,base,f);
            if(sun.skyBlend.w>1.99&&sun.skyBlend.w<2.99)c=vec3(clamp(v.y*2.0,0.0,1.0),side,f); // Debug 2: R view height, G sun side, B dome blend.
        }else if(sun.skyBlend.w>1.99&&sun.skyBlend.w<2.99)c=vec3(clamp(v.y*2.0,0.0,1.0),side,0.0);
        result=vec4(skyDither(min(c,vec3(1.0))),operation(draw.mode.z).a);return;
    }
    vec3 N;
    if(sun.extra.w>0.5){
        N=normalize(cross(dFdx(worldPosition),dFdy(worldPosition)));
        vec3 eye=vec3(sun.normal0.w,sun.normal1.w,sun.normal2.w);
        if(dot(N,eye-worldPosition)<0.0)N=-N;
    }else{
        N=normalize(worldNormal);
    }
    vec3 dp1 = dFdx(worldPosition);
    vec3 dp2 = dFdy(worldPosition);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float det = max(dot(T, T), dot(B, B));
    vec3 n;
    // Scale-independent: near the camera the per-pixel tangents are tiny but still valid
    // (an absolute 1e-10 cut-off dropped the normal map on nearby ground, leaving a flat band).
    bool waterDetail=sun.detail.x>.5&&sun.detail.x<2.5;
    float waterMask=waterDetail&&sun.detail.x>1.5?surfMask(uv):1.0;
    if(waterDetail){
        n=waterNormal(N,waterMask);
    }else if (det > 1e-30) {
        float invmax = inversesqrt(det);
        vec3 map = texture(normalMap, uv).xyz * 2.0 - 1.0;
        float strength = max(sun.extra.y, 0.0);
        vec3 n_tan = normalize(vec3(map.xy * strength, map.z));
        n = normalize(T * (invmax * n_tan.x) + B * (invmax * n_tan.y) + N * n_tan.z);
    } else {
        n = N;
    }
    if(sun.extra.z>0.5){ // Shadow debug: R occlusion, G inside sun region, B sun-space depth bands.
        vec4 q=vec4(shadowPosition,1.0);vec3 s=vec3(dot(sun.shadow0,q),dot(sun.shadow1,q),dot(sun.shadow2,q));
        vec2 uv=s.xy*0.5+0.5;bool inside=all(greaterThan(uv,vec2(0.0)))&&all(lessThan(uv,vec2(1.0)));
        result=vec4(1.0-sunShadow(N),inside?0.6:0.0,fract(s.z*40.0),1.0);return;
    }
    float direct=max(dot(n,sun.direction.xyz),0.0);
    if(sun.shadowParams.x>0.5&&direct>0.0)direct*=sunShadow(N);
    vec3 illumination=sun.ambient.rgb+sun.sunColor.rgb*sun.direction.w*direct;
#ifdef MW34_EFFECT_LIGHTING
    // Original blended sprites retain their emission. Local lights use true
    // world positions, distinct from inverse-transpose normal coordinates.
    vec3 localLight=sun.detailMotion.w<0.0?vec3(0):effectIllumination(shadowPosition,n);
    vec3 rgb = operation(draw.mode.y).rgb*(sun.extra.w>3.5?
        (sun.detailMotion.w<0.0?vec3(1):vec3(sun.detailMotion.w)+localLight):illumination+localLight);
    float fixtureEmissive = max(draw.fogColor.w - 1.0, 0.0);
    if(fixtureEmissive > 0.0){
        vec3 baseCol = operation(draw.mode.y).rgb;
        float lum = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
        float bulb = smoothstep(0.55, 0.9, lum);
        if(sun.emissionParams.x>0.5){
            float mask=0.0;vec2 t=fract(uv);vec2 feather=max(fwidth(t),vec2(0.0005));
            for(int i=0;i<min(int(sun.emissionParams.x),32);++i){vec4 r=sun.emissionRegions[i];vec2 low=smoothstep(r.xy-feather,r.xy+feather,t);vec2 high=1.0-smoothstep(r.zw-feather,r.zw+feather,t);mask=max(mask,low.x*low.y*high.x*high.y);}
            bulb=mask*smoothstep(0.20,0.70,max(texel.r,max(texel.g,texel.b)));
        }
        rgb += texel.rgb * (fixtureEmissive * bulb);
    }
#else
    vec3 rgb = operation(draw.mode.y).rgb*(sun.extra.w>3.5?vec3(1.0):illumination);
#endif
    if(waterDetail){
        vec3 eye=vec3(sun.normal0.w,sun.normal1.w,sun.normal2.w);
        vec3 V=normalize(eye-worldPosition),H=normalize(V+sun.direction.xyz);
        // Restrained sun reflection, masked off foam and sand; alpha and
        // original fog/specular still execute below without modification.
        float glint=pow(max(dot(n,H),0.0),80.0)*max(dot(n,sun.direction.xyz),0.0);
        rgb+=sun.sunColor.rgb*sun.direction.w*glint*.18*sun.detail.y*waterMask;
    }
    float wetness=max(sun.weather.x,0.0);
    if(wetness>0.01&&!waterDetail&&sun.extra.w<=3.5){
        rgb*=mix(1.0,0.74,wetness);
        vec3 eye=vec3(sun.normal0.w,sun.normal1.w,sun.normal2.w);
        vec3 V=normalize(eye-worldPosition),H=normalize(V+sun.direction.xyz);
        float wetGlint=pow(max(dot(n,H),0.0),28.0)*max(dot(n,sun.direction.xyz),0.0);
        rgb+=sun.sunColor.rgb*sun.direction.w*(wetGlint*0.32*wetness);
    }
    float shimmer=max(sun.weather.y,0.0);
    if(shimmer>0.01&&sun.extra.w<=3.5){
        float dFactor=clamp(length(worldPosition-vec3(sun.normal0.w,sun.normal1.w,sun.normal2.w))*0.0008,0.0,1.0);
        rgb+=vec3(0.07,0.06,0.04)*(shimmer*dFactor);
    }
    if(sun.weather.w>0.01&&sun.extra.w<=3.5){
        float lavaProx=clamp((90.0-shadowPosition.y)/110.0,0.0,1.0)*sun.weather.w;
        if(lavaProx>0.0){
            float upFacing=max(-n.y*0.6+0.4,0.0);
            rgb+=vec3(1.15,0.28,0.03)*(lavaProx*upFacing*1.5);
        }
    }
    if(sun.weatherFog.x>0.01&&sun.extra.w<=3.5){
        float hDiff=sun.weatherFog.y-shadowPosition.y;
        if(hDiff>0.0){
            float hFog=clamp(hDiff/90.0,0.0,1.0)*sun.weatherFog.x;
            float dist=length(worldPosition-vec3(sun.normal0.w,sun.normal1.w,sun.normal2.w));
            float vFog=1.0-exp(-dist*0.0016*hFog);
            rgb=mix(rgb,draw.fogColor.rgb,clamp(vFog,0.0,0.85));
        }
    }
    if(sun.weather.z>0.01&&sun.extra.w<=3.5){
        vec3 dustColor=vec3(0.66,0.52,0.34);
        float dustScatter=pow(max(dot(skyView(),sun.direction.xyz),0.0),3.0)*0.45;
        rgb=mix(rgb,dustColor+sun.sunColor.rgb*dustScatter,clamp(sun.weather.z*(1.0-specular.a),0.0,0.72));
    }
    if(sun.ambient.w>0.5)rgb=n*0.5+0.5;
    float alpha = operation(draw.mode.z).a;
    if ((flags & 8u) != 0u) rgb = min(rgb + specular.rgb, 1.0);
    if ((flags & 16u) != 0u) rgb = sun.skyEye.w>0.5?skyDither(mix(hazeColor(skyView()), rgb, specular.a)):mix(draw.fogColor.rgb, rgb, specular.a);
    if ((flags & 32u) != 0u) {
        uint f = draw.mode.w & 255u;
        float r = float((draw.mode.w >> 8) & 255u) / 255.0;
        bool pass = f == 8u || (f == 2u && alpha < r) || (f == 3u && alpha == r) || (f == 4u && alpha <= r) ||
                    (f == 5u && alpha > r) || (f == 6u && alpha != r) || (f == 7u && alpha >= r);
        if (!pass) discard;
    }
    result = vec4(rgb, alpha);
}
