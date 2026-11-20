#version 450

// Only the view-projection matrix is needed: node transforms are baked into the
// vertex data by the asset importer, so the model matrix is the identity.
layout(push_constant) uniform PushConstants {
	mat4 mvp;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec4 outColor;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);
	outNormal = normalize(inNormal);
	outUV = inUV;
	outColor = inColor;
}
