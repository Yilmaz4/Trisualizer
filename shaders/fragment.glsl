#version 460 core

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

in vec3 normal;
in vec3 fragPos;
in vec2 gridPos;

uniform float ambientStrength;
uniform vec3 lightPos;
uniform vec3 cameraPos;

void main() {
	vec3 ambient = vec3(ambientStrength);
	vec3 diffuse = vec3(max(dot(normal, normalize(fragPos - lightPos)), 0.f));
	vec3 specular = 0.5f * vec3(pow(max(dot(normal, normalize(normalize(lightPos - fragPos) + normalize(cameraPos - fragPos))), 0.0), 8));
	fragColor = vec4((ambient + diffuse + specular) * vec3(0.f, 0.5f, 1.f), 1.f);
	if (abs(gridPos.x - floor(gridPos.x)) < 0.04f || abs(gridPos.y - floor(gridPos.y)) < 0.04f) {
		fragColor *= 0.4;
	}
}