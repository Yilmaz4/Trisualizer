#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};
layout(std430, binding = 1) readonly buffer sliderbuffer {
	float sliders[];
};

const float PI = 3.1415926535897932384626433f;
const float e = 2.7182818284590452353602874f;

uniform int grid_res;
uniform float zoomx;
uniform float zoomy;
uniform float zoomz;
uniform vec3 centerPos;

uniform float plane_params[5];

void main() {
	float x = zoomx * ((gl_GlobalInvocationID.x - grid_res / 2.f) / grid_res) + centerPos.x;
    float y = zoomy * ((gl_GlobalInvocationID.y - grid_res / 2.f) / grid_res) + centerPos.y;
	float val = (%s) / zoomz;
	bool in_region = (%s);
    grid[2 * (gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x)] = val;
	grid[2 * (gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x) + 1] = float(in_region);
}