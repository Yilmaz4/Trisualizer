#version 460 core

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	double grid[];
};

in vec3 normal;

void main() {
	fragColor = vec4(1.f);
}