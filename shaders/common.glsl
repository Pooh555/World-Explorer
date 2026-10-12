struct splat_data {
    vec4 mean_opacity;
    vec4 scale_degree;
    vec4 rotation;
    vec4 world_to_sh;
    float sh[48];
};
struct projected_data {
    vec4 center_radius;
    vec4 conic_opacity;
    vec4 color_depth;
};
layout(std430,binding=0) readonly buffer splats_buffer { splat_data splats[]; };
#ifdef WE_VERTEX_STAGE
layout(std430,binding=1) readonly buffer projected_buffer { projected_data projected[]; };
layout(std430,binding=2) readonly buffer keys_in_buffer { uvec2 keys_in[]; };
#else
layout(std430,binding=1) buffer projected_buffer { projected_data projected[]; };
layout(std430,binding=2) buffer keys_in_buffer { uvec2 keys_in[]; };
#endif
layout(std430,binding=3) buffer keys_out_buffer { uvec2 keys_out[]; };
layout(std430,binding=4) buffer histogram_buffer { uint histogram[]; };
layout(push_constant) uniform parameters {
    vec4 right;
    vec4 down;
    vec4 forward;
    vec4 camera_position;
    vec4 intrinsics; // fx, fy, raster half-integer-center cx, cy
    vec4 viewport_clip; // width, height, near, far
    uvec4 count_shift; // splat count, radix shift, group count
} params;
vec3 quaternion_rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.yzw, cross(q.yzw,v) + q.x*v);
}
