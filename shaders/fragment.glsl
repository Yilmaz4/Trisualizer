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
in vec2 gridCoord;

uniform float ambientStrength;
uniform vec3 lightPos;
uniform vec3 cameraPos;
uniform float shininess;
uniform float zoom;
uniform float gridLineDensity;
uniform vec4 color;
uniform int index;
uniform float graph_size;
uniform ivec2 regionSize;
uniform ivec2 windowSize;
uniform vec3 centerPos;
uniform bool tangent_plane;

uniform bool integral;
uniform int region_type;
uniform vec2 corner1;
uniform vec2 corner2;
uniform int integrand_idx;

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
	vec3 normalvec = normal * (int(gl_FrontFacing) * 2 - 1);
	vec3 diffuse = vec3(max(dot(normalvec, normalize(fragPos - lightPos)), 0.f)) * 0.7f;
	vec3 specular = vec3(pow(max(dot(normalvec, normalize(-normalize(lightPos + fragPos) - normalize(cameraPos + fragPos))), 0.0), shininess)) * (shininess / 20.f) * 0.8f;

	if (tangent_plane) fragColor = color;
	else fragColor = vec4((ambientStrength + diffuse + specular) * color.rgb, color.w);

	float partialx = 1.f / tan(acos(dot(vec3(1,0,0), normalize(dot(normal, vec3(1,0,0)) * vec3(1,0,0) + dot(normal, vec3(0,1,0)) * vec3(0,1,0)))));
	float partialy = 1.f / tan(acos(dot(vec3(0,0,1), normalize(dot(normal, vec3(0,0,1)) * vec3(0,0,1) + dot(normal, vec3(0,1,0)) * vec3(0,1,0)))));
	vec2 gradient = vec2(partialx, partialy);

	if (gridLineDensity != 0.f) {
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
			fragColor = vec4(fragColor.rgb * 0.4f, fragColor.w);
		}
	}

	if (integral) {
		if (index != integrand_idx) {
			fragColor = vec4(fragColor.rgb, fragColor.w * 0.1f);
		}
		else {
			vec2 s = step(min(corner1, corner2), gridCoord) * step(gridCoord, max(corner1, corner2));
			fragColor = vec4(fragColor.rgb, fragColor.w * (1.f - (1.f - s.x * s.y) * 0.8f));
		}
	}

	if (!tangent_plane && int(gl_FragCoord.x) % radius == 0 && int(gl_FragCoord.y) % radius == 0) {
		if (integral && index != integrand_idx) return;
		float x = int(gl_FragCoord.x / radius); // idk why i have to subtract regionSize.y, it just works
		float y = int(gl_FragCoord.y / radius);
		float h = int(regionSize.y / radius);
		y = h - y;
		posbuf[6 * int(h * y + x) + 0] = gridCoord.x;
		posbuf[6 * int(h * y + x) + 1] = gridCoord.y;
		posbuf[6 * int(h * y + x) + 2] = fragPos.y * zoom / graph_size;
		posbuf[6 * int(h * y + x) + 3] = float(index);
		posbuf[6 * int(h * y + x) + 4] = partialx;
		posbuf[6 * int(h * y + x) + 5] = partialy;
	}
}