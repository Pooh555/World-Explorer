#version 450
layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput captured_color;
layout(location=0) out vec4 output_color;
void main() {
    // Legacy capture scenes composite in display-encoded RGB. Clamp only after
    // complete accumulation; no gamma reinterpretation or fitted exposure.
    output_color=vec4(clamp(subpassLoad(captured_color).rgb,vec3(0),vec3(1)),1);
}
