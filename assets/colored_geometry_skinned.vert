#version 450

layout(push_constant) uniform Transformation
{
  mat4 mvp;
}
transformation;

layout(set = 0, binding = 0) uniform UBO
{
  mat4 joint_matrix[64];
}
ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;
layout(location = 3) in uvec4 inJoint;
layout(location = 4) in vec4 inWeight;

layout(location = 0) out vec3 outPosition;

void main()
{
  mat4 skin_matrix = inWeight.x * ubo.joint_matrix[inJoint.x] + inWeight.y * ubo.joint_matrix[inJoint.y] +
                     inWeight.z * ubo.joint_matrix[inJoint.z] + inWeight.w * ubo.joint_matrix[inJoint.w];

  gl_Position = transformation.mvp * skin_matrix * vec4(inPosition, 1.0);
  outPosition = inNormal;
}
