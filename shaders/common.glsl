struct splat_data {
    vec4 mean_opacity;
    vec4 scale_degree;
    vec4 rotation;
    vec4 world_to_sh;
#ifdef WE_PACKED_SH
    uint sh[24];
#else
    float sh[48];
#endif
};
struct projected_data {
    vec4 center_radius;
    vec4 conic_opacity;
    vec4 color_depth;
};
layout(std430,binding=0) readonly buffer splats_buffer { splat_data splats[]; };
layout(std430,binding=1) buffer projected_buffer { projected_data projected[]; };
layout(std430,binding=2) buffer keys_in_buffer { uvec2 keys_in[]; };
layout(std430,binding=3) buffer keys_out_buffer { uvec2 keys_out[]; };
layout(std430,binding=4) buffer histogram_buffer { uint histogram[]; };
layout(std430,binding=5) buffer captured_buffer { vec4 captured[]; };
layout(std430,binding=6) buffer overlap_offsets_buffer { uint overlap_offsets[]; };
layout(std430,binding=7) buffer group_offsets_buffer { uint group_offsets[]; };
layout(std430,binding=8) buffer control_buffer { uint control[]; };
layout(std430,binding=9) buffer tile_ranges_buffer { uvec2 tile_ranges[]; };
layout(push_constant) uniform parameters {
    vec4 right; // xyz optical basis row, w camera-from-rebased-world translation
    vec4 down;
    vec4 forward;
    vec4 camera_position;
    vec4 intrinsics; // fx, fy, raster half-integer-center cx, cy
    vec4 viewport_clip; // width, height, near, far
    uvec4 count_shift; // splat count, radix shift, group count
    uvec4 tiles; // first tile x/y, tile width/height
} params;
vec3 quaternion_rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.yzw, cross(q.yzw,v) + q.x*v);
}
uvec4 tile_bounds(projected_data value) {
    vec2 lower=vec2(params.tiles.xy), upper=vec2(params.tiles.xy+params.tiles.zw);
    return uvec4(clamp(floor((value.center_radius.xy-value.center_radius.zw)/16),lower,upper),
                 clamp(ceil((value.center_radius.xy+value.center_radius.zw)/16),lower,upper));
}
