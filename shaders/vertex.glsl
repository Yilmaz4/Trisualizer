#version 460 core

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};

in vec3 aPos;

uniform mat4 vpmat;
uniform int grid_res;
uniform float zoom;
uniform float graph_size;

uniform bool quad;

out vec3 normal;
out vec3 fragPos;
out vec2 gridPos;
out vec2 gridCoord;

void main() {
	if (quad) {
		gl_Position = vec4(aPos, 1.f);
		return;
	}
	const float halfres = grid_res / 2.f;
	float x = (gl_VertexID % grid_res);
	float y = floor(gl_VertexID / grid_res);
	gridPos = vec2((x - halfres) / (grid_res / 10.f), (y - halfres) / (grid_res / 10.f));
	fragPos = vec3((graph_size / grid_res) * (x - halfres), graph_size * grid[gl_VertexID], (graph_size / grid_res) * (y - halfres));
	gridCoord = vec2(zoom * (x - halfres) / grid_res, zoom * (y - halfres) / grid_res);
	
	vec3 v1, v2, v3, v4;
	if (x == grid_res - 1.f) v1 = fragPos;
	else v1 = vec3((graph_size / grid_res) * (x + 1 - halfres), graph_size * grid[int(y * grid_res + (x + 1.f))], (graph_size / grid_res) * (y - halfres));
	if (y == grid_res - 1.f) v2 = fragPos;
	else v2 = vec3((graph_size / grid_res) * (x - halfres), graph_size * grid[int((y + 1.f) * grid_res + x)], (graph_size / grid_res) * (y + 1 - halfres));

	if (x == 0.f) v3 = fragPos;
	else v3 = vec3((graph_size / grid_res) * (x - 1 - halfres), graph_size * grid[int(y * grid_res + (x - 1.f))], (graph_size / grid_res) * (y - halfres));
	if (y == 0.f) v4 = fragPos;
	else v4 = vec3((graph_size / grid_res) * (x - halfres), graph_size * grid[int((y - 1.f) * grid_res + x)], (graph_size / grid_res) * (y - 1 - halfres));

	normal = normalize(cross(v1 - fragPos, v2 - fragPos) + cross(v3 - fragPos, v4 - fragPos));

	gl_Position = vpmat * vec4(fragPos, 1.f);
}