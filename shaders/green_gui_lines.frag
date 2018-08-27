#version 450

layout(push_constant) uniform Transformation
{
  vec4 color;
}
transformation;

layout(location = 0) out vec4 outColor;

void main()
{
  outColor = transformation.color;
}
