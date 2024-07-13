#version 460 core

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

uniform mat4 vpmat;
uniform int grid_res;

out vec3 normal;

void main() {
	int x = gl_VertexID % grid_res - grid_res / 2;
	int y = gl_VertexID / grid_res - grid_res / 2;
	gl_Position = vpmat * vec4(float(x) / grid_res, grid[gl_VertexID], float(y) / grid_res, 1.f);
	normal = vec3(0, 1, 0);
}