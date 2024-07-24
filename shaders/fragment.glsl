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
uniform vec3 centerPos;
uniform bool tangent_plane;

uniform bool quad;
layout(binding = 0) uniform sampler2D frameTex;

void main() {
	if (quad) {
		fragColor = texture(frameTex, gl_FragCoord.xy / windowSize);
		return;
	}
	vec3 normalvec = normal;
	if (!gl_FrontFacing) {
		normalvec *= -1;
	}
	vec3 ambient = vec3(ambientStrength);
	vec3 diffuse = vec3(max(dot(normalvec, normalize(fragPos - lightPos)), 0.f));

	vec3 lightDir = -normalize(lightPos + fragPos);
	vec3 viewDir = -normalize(cameraPos + fragPos);
	vec3 specular = vec3(pow(max(dot(normalvec, normalize(lightDir + viewDir)), 0.0), 8)) * 0.6f;

	if (tangent_plane) fragColor = color;
	else fragColor = vec4((ambient + diffuse + specular) * color.rgb, color.w);

	float partialx = 1.f / tan(acos(dot(vec3(1,0,0), normalize(dot(normalvec, vec3(1,0,0)) * vec3(1,0,0) + dot(normalvec, vec3(0,1,0)) * vec3(0,1,0)))));
	float partialy = 1.f / tan(acos(dot(vec3(0,0,1), normalize(dot(normalvec, vec3(0,0,1)) * vec3(0,0,1) + dot(normalvec, vec3(0,1,0)) * vec3(0,1,0)))));
	vec2 gradient = vec2(partialx, partialy);

	float Rx = gridLineDensity / 100.f;
	float rx = partialx * Rx;
	float hx = sqrt(Rx * Rx + rx * rx);
	float hxp = hx - Rx;
	float rxp = rx * hxp / hx;
	float Rxp = sqrt(hxp * hxp - rxp * rxp);

	float Ry = gridLineDensity / 100.f;
	float ry = partialy * Ry;
	float hy = sqrt(Ry * Ry + ry * ry);
	float hyp = hy - Ry;
	float ryp = ry * hyp / hy;
	float Ryp = sqrt(hyp * hyp - ryp * ryp);

	float gridLines = pow(2.f, floor(log2(zoom)) - gridLineDensity);
	if (abs(gridCoord.x / gridLines - floor(gridCoord.x / gridLines)) < Rx - Rxp ||
		abs(gridCoord.y / gridLines - floor(gridCoord.y / gridLines)) < Ry - Ryp) {
		fragColor = vec4(fragColor.rgb * 0.4, fragColor.w);
	}

	if (!tangent_plane) {
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 0] = gridCoord.x;
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 1] = gridCoord.y;
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 2] = fragPos.y * zoom / graph_size;
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 3] = float(index);
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 4] = partialx;
		posbuf[6 * int(regionSize.y * gl_FragCoord.y + gl_FragCoord.x) + 5] = partialy;
	}
}