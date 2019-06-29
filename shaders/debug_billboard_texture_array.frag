#version 450

layout(set = 0, binding = 0) uniform sampler2DArray Texture;

layout(push_constant) uniform Transformation
{
  layout(offset = 64) uint cascade;
}
transformation;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
  float depthValue = texture(Texture, vec3(inUV.st, transformation.cascade)).r;
  outColor         = vec4(vec3(depthValue), 1.0);
}
