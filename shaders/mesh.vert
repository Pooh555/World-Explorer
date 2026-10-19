#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in uvec3 texture_info;
layout(location=0) out vec3 world_normal;
layout(location=1) out vec2 atlas_uv;
layout(location=2) flat out uvec3 atlas_info;
layout(location=3) flat out uvec2 atlas_tile;
layout(push_constant) uniform parameters {
    vec4 right; vec4 down; vec4 forward; vec4 camera_position;
    vec4 intrinsics; vec4 viewport_clip; uvec4 count_shift; uvec4 tiles;
} params;
void main() {
    vec4 point=vec4(position,1);
    vec3 p=vec3(dot(params.right,point),dot(params.down,point),dot(params.forward,point));
    vec2 clip_xy=2*params.intrinsics.xy*p.xy/params.viewport_clip.xy+
        (2*params.intrinsics.zw/params.viewport_clip.xy-1)*p.z;
    float near_z=params.viewport_clip.z;
    float far_z=params.viewport_clip.w;
    float clip_z=(far_z/(far_z-near_z))*(p.z-near_z);
    gl_Position=vec4(clip_xy,clip_z,p.z);
    world_normal=normal; atlas_uv=uv; atlas_info=texture_info;
    atlas_tile=uvec2(floor(uv/8))*8;
}
