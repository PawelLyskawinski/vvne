#version 450

layout(push_constant) uniform Transformation
{
  mat4 projection;
  mat4 view;
  mat4 model;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;

void main()
{
  outWorldPos = vec3(transformation.model * vec4(inPosition, 1.0));
  gl_Position = transformation.projection * transformation.view * vec4(outWorldPos, 1.0);
  outNormal   = transformation.model * vec4(inNormal, 0.0);
  outTexCoord = inTexCoord;
}
