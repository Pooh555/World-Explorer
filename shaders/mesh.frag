#version 450
layout(location=0) in vec3 world_normal;
layout(location=1) in vec2 atlas_uv;
layout(location=2) flat in uvec3 atlas_info;
layout(location=3) flat in uvec2 atlas_tile;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std430) readonly buffer scene_data { uint words[]; } scene;
layout(push_constant) uniform parameters {
    vec4 right; vec4 down; vec4 forward; vec4 camera_position;
    vec4 intrinsics; vec4 viewport_clip; uvec4 count_shift; uvec4 tiles;
} params;
vec4 texel(ivec2 p) {
    if(any(lessThan(p,ivec2(0))) || uint(p.x)>=atlas_info.y || uint(p.y)>=(atlas_info.z&0x7fffffffU))
        return vec4(0);
    uint offset=atlas_info.x+4*(uint(p.y)*atlas_info.y+uint(p.x));
    return vec4(uintBitsToFloat(scene.words[offset]),uintBitsToFloat(scene.words[offset+1]),
                uintBitsToFloat(scene.words[offset+2]),uintBitsToFloat(scene.words[offset+3]));
}
void main() {
    if(params.count_shift.w==0) {
        float length_squared=dot(world_normal,world_normal);
        vec3 normal=length_squared>1e-12?world_normal*inversesqrt(length_squared):vec3(0);
        color=vec4(0.5+0.5*normal,1); return;
    }
    if(atlas_info.y==0 || atlas_tile.x>=atlas_info.y || atlas_tile.y>=(atlas_info.z&0x7fffffffU)) discard;
    // Flat ownership also bounds helper invocations outside tiny source triangles.
    ivec2 tile=ivec2(atlas_tile);
    vec2 grid=clamp(atlas_uv-vec2(tile)-0.5,vec2(0),vec2(7));
    ivec2 base=ivec2(floor(grid)); vec2 f=grid-vec2(base);
    if((atlas_info.z&0x80000000U)==0) {
        if(base.x+base.y>=7) f=vec2(0);
        else if(base.x+base.y==6 && f.x+f.y>1) f/=f.x+f.y;
    }
    if((atlas_info.z&0x80000000U)!=0) {
        vec3 rgb=vec3(0); float sum=0;
        for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
            float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y);
            if(weight>0) {
                vec4 value=texel(tile+base+ivec2(x,y)); if(value.a!=1) discard;
                rgb+=weight*value.rgb; sum+=weight;
            }
        }
        vec3 linear=clamp(rgb/sum,vec3(0),vec3(1));
        color=vec4(mix(12.92*linear,1.055*pow(linear,vec3(1.0/2.4))-0.055,
                       greaterThan(linear,vec3(0.0031308))),1); return;
    }
    ivec2 p[3]; vec3 w;
    if(f.x+f.y<=1 || base.x+base.y>=6) {
        p[0]=base; p[1]=base+ivec2(1,0); p[2]=base+ivec2(0,1);
        w=vec3(max(0,1-f.x-f.y),f.x,f.y);
    } else {
        p[0]=base+ivec2(1,1); p[1]=base+ivec2(0,1); p[2]=base+ivec2(1,0);
        w=vec3(f.x+f.y-1,1-f.x,1-f.y);
    }
    vec3 result=vec3(0); float total=0;
    for(int i=0;i<3;++i) if(w[i]>0) {
        if(p[i].x+p[i].y>7 || any(lessThan(p[i],ivec2(0))) || any(greaterThan(p[i],ivec2(7)))) discard;
        vec4 sample_color=texel(tile+p[i]); if(sample_color.a!=1) discard;
        result+=w[i]*sample_color.rgb; total+=w[i];
    }
    vec3 linear=clamp(result/total,vec3(0),vec3(1));
    // The shared presentation target is display-encoded UNORM (also used by splats).
    vec3 encoded=mix(12.92*linear,1.055*pow(linear,vec3(1.0/2.4))-0.055,
                     greaterThan(linear,vec3(0.0031308)));
    color=vec4(encoded,1);
}
