#version 450
layout(std430,binding=5) readonly buffer captured_buffer { vec4 captured[]; };
layout(push_constant) uniform parameters {
    vec4 right;
    vec4 down;
    vec4 forward;
    vec4 camera_position;
    vec4 intrinsics;
    vec4 viewport_clip;
    uvec4 count_shift;
    uvec4 tiles;
} params;
layout(location=0) out vec4 output_color;
void main() {
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    // Captured-domain energy remains FP32 until this single SDR conversion.
    output_color=vec4(clamp(captured[pixel.y*uint(params.viewport_clip.x)+pixel.x].rgb,vec3(0),vec3(1)),1);
}
