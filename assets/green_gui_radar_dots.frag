#version 450

layout(push_constant) uniform Transformation
{
  layout(offset = 16) vec4 color;
}
transformation;

layout(location = 0) out vec4 outColor;

void main()
{
  outColor = transformation.color;
}
