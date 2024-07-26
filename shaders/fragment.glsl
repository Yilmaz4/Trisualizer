#version 460 core

layout(early_fragment_tests) in;

out vec4 fragColor;

layout(std430, binding = 0) volatile buffer gridbuffer {
	float grid[];
};
layout(std430, binding = 1) volatile buffer posbuffer {
	float posbuf[];
};
layout(std430, binding = 2) readonly buffer kernel {
    float weights[];
};
uniform int radius;

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
		if (radius > 1) {
			vec3 blurredColor = vec3(0.0);
			int kernelSize = 2 * radius + 1;
			for (int i = -radius; i <= radius; i++) {
				for (int j = -radius; j <= radius; j++) {
					vec3 c = texture(frameTex, (gl_FragCoord.xy * radius + vec2(i, j)) / windowSize).rgb;
					blurredColor += c * weights[(i + radius) * kernelSize + (j + radius)];
				}
			}
			fragColor = vec4(blurredColor, 1.f);
		} else {
			fragColor = vec4(texture(frameTex, gl_FragCoord.xy / windowSize).rgb, 1.f);
		}
		return;
	}
	vec3 normalvec = normal;
	if (!gl_FrontFacing) {
		normalvec *= -1;
	}
	vec3 diffuse = vec3(max(dot(normalvec, normalize(fragPos - lightPos)), 0.f));
	vec3 specular = vec3(pow(max(dot(normalvec, normalize(-normalize(lightPos + fragPos) - normalize(cameraPos + fragPos))), 0.0), 8)) * 0.6f;

	if (tangent_plane) fragColor = color;
	else fragColor = vec4((ambientStrength + diffuse + specular) * color.rgb, color.w);

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

	if (!tangent_plane && int(gl_FragCoord.x) % radius == 0 && int(gl_FragCoord.y) % radius == 0) {
		float x = int(gl_FragCoord.x / radius) - 600;
		float y = int(gl_FragCoord.y / radius);
		float h = int(regionSize.y / radius);
		posbuf[6 * int(h * y + x) + 0] = gridCoord.x;
		posbuf[6 * int(h * y + x) + 1] = gridCoord.y;
		posbuf[6 * int(h * y + x) + 2] = fragPos.y * zoom / graph_size;
		posbuf[6 * int(h * y + x) + 3] = float(index);
		posbuf[6 * int(h * y + x) + 4] = partialx;
		posbuf[6 * int(h * y + x) + 5] = partialy;
	}
}