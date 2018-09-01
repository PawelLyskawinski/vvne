#version 450

layout(location = 0) in vec3 inPos;
layout(constant_id = 0) const int SHADOW_MAP_CASCADE_COUNT = 4;

layout(set = 0, binding = 0) uniform UBO
{
  mat4 cascade_view_proj_matrix[SHADOW_MAP_CASCADE_COUNT];
}
ubo;

layout(push_constant) uniform PushConst
{
  mat4 model;
  uint cascade_index;
}
push_const;

void main()
{
  gl_Position = ubo.cascade_view_proj_matrix[push_const.cascade_index] * push_const.model * vec4(inPos, 1.0);
}