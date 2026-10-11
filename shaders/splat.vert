#version 450
#extension GL_GOOGLE_include_directive : require
#define WE_VERTEX_STAGE
#include "common.glsl"
layout(location=0) out vec2 distance_pixels;
layout(location=1) flat out vec4 conic_opacity;
layout(location=2) flat out vec3 color;
void main() {
    const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    uint id=keys_in[gl_InstanceIndex].y;
    projected_data splat=projected[id];
    distance_pixels=corners[gl_VertexIndex]*splat.center_radius.zw;
    conic_opacity=splat.conic_opacity;
    color=splat.color_depth.rgb;
    // Vulkan raster pixel centers are half-integer; canonical cameras use integer centers.
    vec2 pixel=splat.center_radius.xy+distance_pixels+vec2(0.5);
    gl_Position=vec4(pixel/params.viewport_clip.xy*2-1,0,1);
}
