#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};
layout(std430, binding = 1) readonly buffer sliderbuffer {
	float sliders[];
};

uniform int grid_res;
uniform float zoom;
uniform vec3 centerPos;

uniform float plane_params[5];

void main() {
	float x = zoom * ((gl_GlobalInvocationID.x - grid_res / 2.f) / grid_res) + centerPos.x;
    float y = zoom * ((gl_GlobalInvocationID.y - grid_res / 2.f) / grid_res) + centerPos.y;
	float val = (%s) / zoom;
    grid[gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x] = val;
}