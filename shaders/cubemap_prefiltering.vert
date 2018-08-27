#version 450

layout(push_constant) uniform Transformation
{
  mat4 mvp;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outWorldPos;

void main()
{
  outWorldPos = inPosition;
  gl_Position = transformation.mvp * vec4(outWorldPos, 1.0);
}
