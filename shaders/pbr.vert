#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 uv;
layout(location=0) out vec3 view_position;
layout(location=1) out vec3 view_normal;
layout(location=2) out vec4 view_tangent;
layout(location=3) out vec2 texture_uv;
layout(set=0,binding=0,std430) readonly buffer scene_data { uint words[]; } scene;
layout(push_constant) uniform parameters {
    vec4 right; vec4 down; vec4 forward; vec4 intrinsics; vec4 viewport_clip;
    uvec4 draw; vec4 light; vec4 ambient_exposure;
} params;
vec4 row(uint at) { return vec4(uintBitsToFloat(scene.words[at]),uintBitsToFloat(scene.words[at+1]),
                              uintBitsToFloat(scene.words[at+2]),uintBitsToFloat(scene.words[at+3])); }
void main() {
    uint base=params.draw.x+12*gl_InstanceIndex;
    vec4 r0=row(base),r1=row(base+4),r2=row(base+8);
    vec4 p=vec4(position,1);
    vec4 world=vec4(dot(r0,p),dot(r1,p),dot(r2,p),1);
    view_position=vec3(dot(params.right,world),dot(params.down,world),dot(params.forward,world));
    mat3 camera=mat3(params.right.xyz,params.down.xyz,params.forward.xyz);
    vec3 n=vec3(dot(r0.xyz,normal),dot(r1.xyz,normal),dot(r2.xyz,normal));
    vec3 t=vec3(dot(r0.xyz,tangent.xyz),dot(r1.xyz,tangent.xyz),dot(r2.xyz,tangent.xyz));
    view_normal=transpose(camera)*normalize(n);
    view_tangent=vec4(transpose(camera)*normalize(t),tangent.w);
    texture_uv=uv;
    vec3 q=view_position;
    vec2 clip_xy=2*params.intrinsics.xy*q.xy/params.viewport_clip.xy+
        (2*params.intrinsics.zw/params.viewport_clip.xy-1)*q.z;
    float near_z=params.viewport_clip.z, far_z=params.viewport_clip.w;
    gl_Position=vec4(clip_xy,(far_z/(far_z-near_z))*(q.z-near_z),q.z);
}
