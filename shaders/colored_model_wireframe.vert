#version 450

layout(push_constant) uniform PushConst
{
  mat4 mvp;
}
push_const;

layout(location = 0) in vec3 inPosition;

void main()
{
  gl_Position = push_const.mvp * vec4(inPosition, 1.0);
}
