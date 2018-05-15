#version 450

layout(push_constant) uniform Parameters
{
  layout(offset = 64) vec3 color;
}
parameters;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec4 outColor;

void main()
{
  outColor = vec4(mix(parameters.color, normalize(inPosition), 0.4), 1.0);
}
