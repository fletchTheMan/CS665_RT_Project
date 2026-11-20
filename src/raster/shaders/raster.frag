#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 normal = normalize(inNormal);
	vec3 lightDirection = normalize(vec3(0.35, -0.8, 0.5));
	float diffuse = max(dot(normal, lightDirection), 0.0);
	vec3 lit = inColor.rgb * (0.25 + 0.75 * diffuse);
	outColor = vec4(lit, inColor.a);
}
