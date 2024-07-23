#version 460 core

layout(early_fragment_tests) in;

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};
layout(std430, binding = 1) volatile buffer posbuffer {
	float posbuf[];
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
uniform vec4 color;
uniform int index;
uniform float graph_size;
uniform ivec2 regionSize;
uniform ivec2 windowSize;
uniform vec2 centerPos;

uniform bool quad;
layout(binding = 0) uniform sampler2D frameTex;

void main() {
	if (quad) {
		fragColor = texture(frameTex, gl_FragCoord.xy / windowSize);
		return;
	}
	vec3 normalVec = normal;
	if (!gl_FrontFacing) {
		normalVec *= -1;
	}
	vec3 ambient = vec3(ambientStrength);
	vec3 diffuse = vec3(max(dot(normalVec, normalize(fragPos - lightPos)), 0.f));

	vec3 lightDir = -normalize(lightPos + fragPos);
	vec3 viewDir = -normalize(cameraPos + fragPos);
	vec3 specular = vec3(pow(max(dot(normalVec, normalize(lightDir + viewDir)), 0.0), 8)) * 0.8f;
	
	fragColor = vec4((ambient + diffuse + specular) * color.rgb, color.w);

	float gridLines = pow(2.f, floor(log2(zoom)) - gridLineDensity);

	if (abs(gridCoord.x / gridLines - floor(gridCoord.x / gridLines)) < gridLineDensity / 100.f ||
		abs(gridCoord.y / gridLines - floor(gridCoord.y / gridLines)) < gridLineDensity / 100.f) {
		fragColor = vec4(fragColor.rgb * 0.4, fragColor.w);
	}
	posbuf[4 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 0] = gridCoord.x;
	posbuf[4 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 1] = gridCoord.y;
	posbuf[4 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 2] = fragPos.y * zoom / graph_size;
	posbuf[4 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 3] = intBitsToFloat(index);
}