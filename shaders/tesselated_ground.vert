#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outTexCoord;

void main()
{
  gl_Position = vec4(inPosition, 1.0f);
  outNormal   = vec4(inNormal, 1.0);
  outTexCoord = inTexCoord;
}
