#version 450

layout(set = 0, binding = 0) uniform sampler2D Texture;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
  float depthValue = texture(Texture, inUV.st).r;
  outColor = vec4(vec3(depthValue), 1.0);
}
