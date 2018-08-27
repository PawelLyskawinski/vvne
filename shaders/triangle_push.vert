#version 450

layout(push_constant) uniform Transformation
{
  mat4 projection;
  mat4 view;
  mat4 model;
}
transformation;

layout(set = 3, binding = 0) uniform UBO
{
  mat4 lightSpaceMatrix;
}
ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outFragPosLightSpace;

const mat4 bias_matrix = mat4(0.5, 0.0, 0.0, 0.0, // 0
                              0.0, 0.5, 0.0, 0.0, // 1
                              0.0, 0.0, 1.0, 0.0, // 2
                              0.5, 0.5, 0.0, 1.0  // 3
);

void main()
{
  outWorldPos          = vec3(transformation.model * vec4(inPosition, 1.0));
  gl_Position          = transformation.projection * transformation.view * vec4(outWorldPos, 1.0);
  outNormal            = transformation.model * vec4(inNormal, 0.0);
  outTexCoord          = inTexCoord;
  outFragPosLightSpace = bias_matrix * ubo.lightSpaceMatrix * vec4(outWorldPos, 1.0);
  // outFragPosLightSpace = vec4(outWorldPos + ubo.lightSpaceMatrix[1][0], 1.0);
}
