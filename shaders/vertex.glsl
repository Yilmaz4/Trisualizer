#version 460 core

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

uniform mat4 vpmat;

void main() {
	int x = gl_VertexID % 100 - 50;
	int y = gl_VertexID / 100 - 50;
	gl_Position = vpmat * vec4(float(x) / 1e+2f, grid[gl_VertexID], float(y) / 1e+2f, 1.f);
}