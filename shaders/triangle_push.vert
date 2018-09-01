#version 450

layout(push_constant) uniform PushConst
{
  mat4 projection;
  mat4 view;
  mat4 model;
}
push_const;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outViewPos;

void main()
{
  outWorldPos = vec3(push_const.model * vec4(inPosition, 1.0));
  gl_Position = push_const.projection * push_const.view * vec4(outWorldPos, 1.0);
  outNormal   = push_const.model * vec4(inNormal, 0.0);
  //outViewPos  = vec4(push_const.view * vec4(inPosition, 1.0)).xyz;
  outViewPos  = gl_Position.xyz;
  outTexCoord = inTexCoord;
}
