#version 460 core

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

in vec3 normal;
in vec3 fragPos;

uniform vec3 ambientStrength;
uniform vec3 lightPos;

void main() {
	vec3 diffuse = vec3(max(dot(normal, normalize(fragPos - lightPos)), 0.f));
	fragColor = vec4((vec3(ambientStrength) + diffuse) * vec3(0.f, 0.5f, 1.f), 1.f);
}