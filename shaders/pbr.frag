#version 450
layout(location=0) in vec3 view_position;
layout(location=1) in vec3 view_normal;
layout(location=2) in vec4 view_tangent;
layout(location=3) in vec2 texture_uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std430) readonly buffer scene_data { uint words[]; } scene;
layout(push_constant) uniform parameters {
    vec4 right; vec4 down; vec4 forward; vec4 intrinsics; vec4 viewport_clip;
    uvec4 draw; vec4 light; vec4 ambient_exposure;
} params;
vec3 linear_rgb(vec3 x) { return mix(x/12.92,pow((x+0.055)/1.055,vec3(2.4)),greaterThan(x,vec3(0.04045))); }
vec3 encoded_rgb(vec3 x) { return mix(12.92*x,1.055*pow(x,vec3(1/2.4))-0.055,greaterThan(x,vec3(0.0031308))); }
vec3 texel(uint role,ivec2 p) {
    uint material=params.draw.y;
    ivec2 size=ivec2(scene.words[material+4],scene.words[material+5]);
    if(scene.words[material+7]!=0) p=(p%size+size)%size;
    else p=clamp(p,ivec2(0),size-1);
    uint at=scene.words[material+role]+uint(p.y)*uint(size.x)+uint(p.x);
    vec3 value=unpackUnorm4x8(scene.words[at]).rgb;
    return role==0 || role==3?linear_rgb(value):value;
}
vec3 sample_map(uint role) {
    uint material=params.draw.y;
    vec2 size=vec2(scene.words[material+4],scene.words[material+5]);
    vec2 uv=scene.words[material+7]!=0?fract(texture_uv):clamp(texture_uv,vec2(0),vec2(1));
    if(scene.words[material+6]==0) return texel(role,ivec2(floor(uv*size)));
    vec2 grid=uv*size-0.5;
    ivec2 p=ivec2(floor(grid)); vec2 f=fract(grid);
    return mix(mix(texel(role,p),texel(role,p+ivec2(1,0)),f.x),
               mix(texel(role,p+ivec2(0,1)),texel(role,p+ivec2(1,1)),f.x),f.y);
}
vec3 unit_or(vec3 value, vec3 fallback) {
    float squared=dot(value,value);
    return squared>1e-12?value*inversesqrt(squared):fallback;
}
void main() {
    vec3 n=unit_or(view_normal,vec3(0,0,-1));
    vec3 fallback=abs(n.z)<0.999?normalize(cross(vec3(0,0,1),n)):vec3(1,0,0);
    vec3 t=unit_or(view_tangent.xyz-n*dot(n,view_tangent.xyz),fallback);
    vec3 b=cross(n,t)*(view_tangent.w<0?-1:1);
    vec3 mapped=normalize(sample_map(2)*2-1);
    n=normalize(t*mapped.x+b*mapped.y+n*mapped.z);
    if(!gl_FrontFacing) n=-n;
    vec3 v=normalize(-view_position),l=normalize(params.light.xyz);
    vec3 base=sample_map(0),orm=sample_map(1),emission=sample_map(3);
    float metallic=orm.b, roughness=orm.g;
    vec3 f0=mix(vec3(0.04),base,metallic);
    vec3 radiance=emission+(base*(1-metallic)+f0)*params.ambient_exposure.rgb*orm.r;
    float nv=clamp(dot(n,v),0,1),nl=clamp(dot(n,l),0,1);
    if(nv>0 && nl>0) {
        vec3 h=normalize(v+l);
        float nh=clamp(dot(n,h),0,1),vh=clamp(dot(v,h),0,1);
        float a=max(roughness*roughness,0.0001),a2=a*a;
        float denominator=(1-nh*nh)+a2*nh*nh;
        float distribution=a2/(3.141592653589793*denominator*denominator);
        float visibility=0.5/(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2));
        vec3 fresnel=f0+(1-f0)*pow(1-vh,5);
        radiance+=nl*params.light.w*((1-fresnel)*(1-metallic)*base/3.141592653589793+
                                   distribution*visibility*fresnel);
    }
    vec3 exposed=max(radiance*params.ambient_exposure.w,vec3(0));
    color=vec4(encoded_rgb(exposed/(1+exposed)),1);
}
