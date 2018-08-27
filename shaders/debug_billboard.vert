#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 outUV;

layout(push_constant) uniform Transformation
{
  mat4 mvp;
}
transformation;

void main()
{
  gl_Position = transformation.mvp * vec4(inPos, 0.0, 1.0);
  outUV       = inUV;
}
