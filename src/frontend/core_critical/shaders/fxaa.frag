#version 450
// Authored FXAA implementation: contrast rejection, edge orientation, endpoint
// search and a conservative subpixel filter. Algorithm: NVIDIA FXAA whitepaper.
// This is not a copy of, or a claim to be, NVIDIA's FXAA 3.11 implementation.
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(push_constant) uniform Quality { vec2 pixel; } q;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
float luma(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
float at(vec2 p) { return luma(textureLod(sourceImage,p,0.0).rgb); }
void main() {
    vec4 center=textureLod(sourceImage,uv,0.0);
    float m=luma(center.rgb);
    float n=at(uv+q.pixel*vec2(0,-1)),s=at(uv+q.pixel*vec2(0,1));
    float w=at(uv+q.pixel*vec2(-1,0)),e=at(uv+q.pixel*vec2(1,0));
    float low=min(m,min(min(n,s),min(w,e))),high=max(m,max(max(n,s),max(w,e)));
    float range=high-low;
    if(range<max(0.03125,high*0.166667)) { outColor=center;return; }
    float nw=at(uv+q.pixel*vec2(-1,-1)),ne=at(uv+q.pixel*vec2(1,-1));
    float sw=at(uv+q.pixel*vec2(-1,1)),se=at(uv+q.pixel*vec2(1,1));
    float horizontal=abs(nw+ne-2*n)+2*abs(w+e-2*m)+abs(sw+se-2*s);
    float vertical=abs(nw+sw-2*w)+2*abs(n+s-2*m)+abs(ne+se-2*e);
    bool h=horizontal>=vertical;
    float negative=h?n:w,positive=h?s:e;
    float gn=abs(negative-m),gp=abs(positive-m);
    bool useNegative=gn>=gp;
    float average=0.5*(m+(useNegative?negative:positive));
    vec2 normal=h?vec2(0,q.pixel.y):vec2(q.pixel.x,0);
    if(useNegative) normal=-normal;
    vec2 tangent=h?vec2(q.pixel.x,0):vec2(0,q.pixel.y);
    vec2 origin=uv+0.5*normal,lo=origin-tangent,hi=origin+tangent;
    float dl=at(lo)-average,dh=at(hi)-average,threshold=0.25*max(gn,gp);
    bool endLo=abs(dl)>=threshold,endHi=abs(dh)>=threshold;
    const float steps[10]=float[10](1.0,1.5,2.0,2.0,2.0,2.0,2.0,2.0,4.0,8.0);
    for(int i=0;i<10&&!(endLo&&endHi);++i) {
        if(!endLo) { lo-=tangent*steps[i];dl=at(lo)-average;endLo=abs(dl)>=threshold; }
        if(!endHi) { hi+=tangent*steps[i];dh=at(hi)-average;endHi=abs(dh)>=threshold; }
    }
    float distLo=h?uv.x-lo.x:uv.y-lo.y,distHi=h?hi.x-uv.x:hi.y-uv.y;
    bool closerLo=distLo<distHi;
    float nearest=closerLo?dl:dh;
    bool found=closerLo?endLo:endHi;
    float edge=found&&((nearest<0.0)!=(m<average))?0.5-min(distLo,distHi)/(distLo+distHi):0.0;
    float sub=clamp(abs((2.0*(n+s+w+e)+nw+ne+sw+se)/12.0-m)/range,0.0,1.0);
    sub=sub*sub*(3.0-2.0*sub);sub=sub*sub*0.75;
    outColor=vec4(textureLod(sourceImage,uv+normal*max(edge,sub),0.0).rgb,center.a);
}
