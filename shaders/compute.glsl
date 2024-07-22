#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};

uniform int grid_res;
uniform float zoom;

uniform float a;
uniform float b;
uniform float c;
uniform float d;

void main() {
	float x = zoom * ((gl_GlobalInvocationID.x - grid_res / 2.f) / grid_res);
    float y = zoom * ((gl_GlobalInvocationID.y - grid_res / 2.f) / grid_res);
    grid[gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x] = (%s) / zoom;
}