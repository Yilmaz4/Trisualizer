#version 460 core

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

uniform mat4 vpmat;
uniform int grid_res;
uniform float zoom;

out vec3 normal;
out vec3 fragPos;
out vec2 gridPos;
out vec2 gridCoord;

void main() {
	const int halfres = grid_res / 2;
	float x = (gl_VertexID % grid_res);
	float y = (gl_VertexID / grid_res);
	gridPos = vec2((x - halfres) / (grid_res / 10.f), (y - halfres) / (grid_res / 10.f));
	fragPos = vec3((x - halfres) / grid_res, grid[gl_VertexID], (y - halfres) / grid_res);
	gridCoord = vec2(zoom * (x - halfres) / grid_res, zoom * (y - halfres) / grid_res);
	
	vec3 v1, v2, v3, v4;
	if (x == grid_res - 1) v1 = fragPos;
	else v1 = vec3((x + 1 - halfres) / grid_res, grid[int(y * grid_res + (x + 1))], (y - halfres) / grid_res);
	if (y == grid_res - 1) v2 = fragPos;
	else v2 = vec3((x - halfres) / grid_res, grid[int((y + 1) * grid_res + x)], (y + 1 - halfres) / grid_res);

	if (x == 0) v3 = fragPos;
	else v3 = vec3((x - 1 - halfres) / grid_res, grid[int(y * grid_res + (x - 1))], (y - halfres) / grid_res);
	if (y == 0) v4 = fragPos;
	else v4 = vec3((x - halfres) / grid_res, grid[int((y - 1) * grid_res + x)], (y - 1 - halfres) / grid_res);

	normal = normalize(cross(v1 - fragPos, v2 - fragPos) + cross(v3 - fragPos, v4 - fragPos));

	gl_Position = vpmat * vec4(fragPos, 1.f);
}