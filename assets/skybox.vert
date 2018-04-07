#version 450

layout(push_constant) uniform Transformation
{
  mat4 mvp;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 0) out vec3 outUVW;

void main()
{
  gl_Position = transformation.mvp * vec4(inPosition, 1.0);
  outUVW      = inPosition;
}