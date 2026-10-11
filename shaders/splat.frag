#version 450
layout(location=0) in vec2 distance_pixels;
layout(location=1) flat in vec4 conic_opacity;
layout(location=2) flat in vec3 color;
layout(location=0) out vec4 output_color;
void main() {
    float quadratic=conic_opacity.x*distance_pixels.x*distance_pixels.x+2*conic_opacity.y*distance_pixels.x*distance_pixels.y+conic_opacity.z*distance_pixels.y*distance_pixels.y;
    float alpha=min(0.999,conic_opacity.w*exp(-0.5*quadratic));
    if(alpha<1.0/255.0||quadratic<0) discard;
    output_color=vec4(color*alpha,alpha);
}
