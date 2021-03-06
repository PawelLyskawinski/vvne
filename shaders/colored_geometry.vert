#version 450

layout(push_constant) uniform Transformation
{
  mat4 mvp;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outPosition;

void main()
{
  gl_Position = transformation.mvp * vec4(inPosition, 1.0);
  outPosition = gl_Position.xyz;
}
