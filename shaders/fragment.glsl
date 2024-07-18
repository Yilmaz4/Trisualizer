#version 460 core

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};

in vec3 normal;
in vec3 fragPos;
in vec2 gridPos;
in vec2 gridCoord;

uniform float ambientStrength;
uniform vec3 lightPos;
uniform vec3 cameraPos;
uniform float zoom;
uniform float gridLineDensity;
uniform vec3 color;

void main() {
	vec3 ambient = vec3(ambientStrength);
	vec3 diffuse = vec3(max(dot(normal, normalize(fragPos - lightPos)), 0.f));
	vec3 specular = 0.5f * vec3(pow(max(dot(normal, normalize(normalize(lightPos - fragPos) + normalize(cameraPos - fragPos))), 0.0), 8));
	
	fragColor = vec4((ambient + diffuse + specular) * color, 1.f);

	float gridLines = pow(2.f, floor(log2(zoom)) - gridLineDensity);

	if (abs(gridCoord.x / gridLines - floor(gridCoord.x / gridLines)) < gridLineDensity / 100.f ||
		abs(gridCoord.y / gridLines - floor(gridCoord.y / gridLines)) < gridLineDensity / 100.f) {
		fragColor *= 0.4;
	}
}