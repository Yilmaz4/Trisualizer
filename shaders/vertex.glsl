#version 460 core

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};

in vec3 aPos;

uniform mat4 vpmat;
uniform int grid_res;
uniform float zoomx;
uniform float zoomy;
uniform float zoomz;
uniform float graph_size;
uniform vec3 centerPos;

uniform bool quad;

out vec3 normal;
out vec3 fragPos;
out vec2 gridCoord;
flat out float inRegion;

void main() {
	if (quad) {
		gl_Position = vec4(aPos, 1.f);
		return;
	}
	const float gridres = grid_res + 2;
	const float halfres = gridres / 2.f;
	float x = floor(gl_VertexID / grid_res) + 1;
	float y = (gl_VertexID % grid_res) + 1;

	fragPos = vec3((graph_size / gridres) * (x - halfres), graph_size * grid[2 * int(y * gridres + x)], (graph_size / gridres) * (y - halfres));
	gridCoord = vec2(zoomx * (x - halfres) / gridres, zoomy * (y - halfres) / gridres) + centerPos.xy;
	inRegion = grid[2 * int(y * gridres + x) + 1];
	
	vec3 v1 = vec3((graph_size / gridres) * (x + 1 - halfres), graph_size * grid[2 * int(y * gridres + (x + 1.f))], (graph_size / gridres) * (y - halfres));
	vec3 v2 = vec3((graph_size / gridres) * (x - halfres), graph_size * grid[2 * int((y + 1.f) * gridres + x)], (graph_size / gridres) * (y + 1 - halfres));

	vec3 v3 = vec3((graph_size / gridres) * (x - 1 - halfres), graph_size * grid[2 * int(y * gridres + (x - 1.f))], (graph_size / gridres) * (y - halfres));
	vec3 v4 = vec3((graph_size / gridres) * (x - halfres), graph_size * grid[2 * int((y - 1.f) * gridres + x)], (graph_size / gridres) * (y - 1 - halfres));

	normal = normalize(cross(v1 - fragPos, v2 - fragPos) + cross(v3 - fragPos, v4 - fragPos));

	gl_Position = vpmat * vec4(fragPos.x, fragPos.y - centerPos.z / zoomz * graph_size, fragPos.z, 1.f);
}