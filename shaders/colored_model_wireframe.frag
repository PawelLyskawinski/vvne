#version 450

layout(push_constant) uniform PushConst
{
  layout(offset = 64) vec3 color;
}
push_const;

layout(location = 0) out vec4 outColor;

void main()
{
  outColor = vec4(push_const.color, 0.5);
}
