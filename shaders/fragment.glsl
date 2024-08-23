#version 460 core

out vec4 fragColor;

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
flat in float inRegion;

uniform float ambientStrength;
uniform vec3 lightPos;
uniform vec3 cameraPos;
uniform float shininess;
uniform float zoomx;
uniform float zoomy;
uniform float zoomz;
uniform float gridLineDensity;
uniform int coloring;
uniform vec4 color;
uniform vec4 secondary_color;
uniform int index;
uniform float graph_size;
uniform ivec2 regionSize;
uniform ivec2 windowSize;
uniform vec3 centerPos;
uniform bool tangent_plane;
uniform bool shading;

uniform bool integral;
uniform int region_type;
uniform vec2 corner1;
uniform vec2 corner2;
uniform int integrand_idx;

uniform bool quad;
layout(binding = 0) uniform sampler2D frameTex;
layout(binding = 1) uniform sampler2D prevZBuffer;

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
	float z = fragPos.y * zoomz / graph_size;
	vec3 normalvec = normal * (int(!gl_FrontFacing) * 2 - 1);
	float partialx = 1.f / tan(acos(dot(vec3(1, 0, 0), normalize(dot(normal, vec3(1, 0, 0)) * vec3(1, 0, 0) + dot(normal, vec3(0, 1, 0)) * vec3(0, 1, 0)))));
	float partialy = 1.f / tan(acos(dot(vec3(0, 0, 1), normalize(dot(normal, vec3(0, 0, 1)) * vec3(0, 0, 1) + dot(normal, vec3(0, 1, 0)) * vec3(0, 1, 0)))));
	vec2 gradient = vec2(partialx, partialy);

	vec2 zrange = vec2(zoomz / 2.f, -zoomz / 2.f) + centerPos.z;
	vec3 grad3d = vec3(gradient, partialx * partialx + partialy * partialy);
	float angle = acos(length(gradient) / length(grad3d));
	
	switch (coloring) {
	case 0:
		fragColor = color;
		break;
	case 1:
		fragColor = mix(color, secondary_color, float(!gl_FrontFacing));
		break;
	case 2:
		fragColor = mix(color, secondary_color, (z - zrange.x) / (zrange.y - zrange.x));
		break;
	case 3:
		if (isnan(angle) || isinf(angle)) angle = 0.f;
		fragColor = mix(color, secondary_color, angle / 1.57079632f);
		break;
	case 4:
		fragColor = vec4(normalvec * 0.5f + 0.5f, 1.f);
	}

	if (abs(z - centerPos.z) > zoomz / 2.f && index != 0) discard;
	
	if (!tangent_plane && shading) {
		vec3 diffuse = vec3(max(dot(normalvec, normalize(fragPos + lightPos)), 0.f)) * 0.7f;
		vec3 specular = vec3(pow(max(dot(normalvec, normalize(normalize(fragPos + lightPos) + normalize(fragPos - cameraPos))), 0.0), shininess)) * (shininess / 20.f) * 0.4f;
		fragColor = vec4((ambientStrength + diffuse) * fragColor.rgb + specular * vec3(int(gl_FrontFacing)), fragColor.w);
	}

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

		float spacing = pow(2.f, floor(log2((zoomx + zoomy) / 2.f)) - gridLineDensity);
		if (abs(gridCoord.x / spacing - floor(gridCoord.x / spacing)) < Rx - Rxp ||
			abs(gridCoord.y / spacing - floor(gridCoord.y / spacing)) < Ry - Ryp) {
			fragColor = vec4(fragColor.rgb * 0.4f, fragColor.w);
		}
	}

	if (integral) {
		if (index != integrand_idx) {
			fragColor = vec4(fragColor.rgb, fragColor.w * 0.1f);
		} else if (region_type == -1) {
			vec2 s = step(min(corner1, corner2), gridCoord) * step(gridCoord, max(corner1, corner2));
			fragColor = vec4(fragColor.rgb * (1.f - (1.f - s.x * s.y) * 0.8f), fragColor.w);
		} else {
			fragColor = vec4(fragColor.rgb * (inRegion * 0.8f + 0.2f), fragColor.w);
		}
	}

	float prevDepth = texture(prevZBuffer, (gl_FragCoord.xy) / windowSize).r;
	if (!tangent_plane && int(gl_FragCoord.x) % radius == 0 && int(gl_FragCoord.y) % radius == 0 && gl_FragCoord.z == prevDepth) {
		if (integral && index != integrand_idx) return;
		float x = floor((gl_FragCoord.x - windowSize.x + regionSize.x) / radius);
		float y = floor(gl_FragCoord.y / radius);
		float w = floor(regionSize.x / radius);
		y = floor(regionSize.y / radius) - y;
		posbuf[6 * int(w * y + x) + 0] = gridCoord.x;
		posbuf[6 * int(w * y + x) + 1] = gridCoord.y;
		posbuf[6 * int(w * y + x) + 2] = z;
		posbuf[6 * int(w * y + x) + 3] = float(index);
		posbuf[6 * int(w * y + x) + 4] = partialx;
		posbuf[6 * int(w * y + x) + 5] = partialy;
	}
}