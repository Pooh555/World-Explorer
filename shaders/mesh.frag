#version 450
layout(location=0) in vec3 world_normal;
layout(location=0) out vec4 color;
void main() {
    float length_squared=dot(world_normal,world_normal);
    vec3 normal=length_squared>1e-12?world_normal*inversesqrt(length_squared):vec3(0);
    color=vec4(0.5+0.5*normal,1);
}
