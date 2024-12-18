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

float cot(float x) {
	return 1.f / tan(x);
}
float sec(float x) {
	return 1.f / cos(x);
}
float csc(float x) {
	return 1.f / sin(x);
}

float f(float x, float y) {
	return (%s);
}
void main() {
	float x = zoomx * ((gl_GlobalInvocationID.x - grid_res / 2.f) / grid_res) + centerPos.x;
    float y = zoomy * ((gl_GlobalInvocationID.y - grid_res / 2.f) / grid_res) + centerPos.y;
	float px, py;
	if (%s) {
		px = (f(x + 0.001f, y) - f(x - 0.001f, y)) / 0.002f;
		py = (f(x, y + 0.001f) - f(x, y - 0.001f)) / 0.002f;
	}
	%s float t = atan(-y, -x) + PI;
	float z = f(x, y);
	float val = (%s) / zoomz;
	bool in_region = (%s);
    grid[2 * (gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x)] = val;
	grid[2 * (gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x) + 1] = float(in_region);
}