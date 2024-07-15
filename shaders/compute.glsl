#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};
uniform int grid_res;
uniform float zoom;
uniform float time;

void main() {
	float x = zoom * (gl_GlobalInvocationID.x - grid_res / 2.f) / grid_res;
    float y = zoom * (gl_GlobalInvocationID.y - grid_res / 2.f) / grid_res;
    grid[gl_GlobalInvocationID.y * grid_res + gl_GlobalInvocationID.x] = log(x * x + y * y) / zoom;
}