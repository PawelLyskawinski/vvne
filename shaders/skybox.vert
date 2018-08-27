#version 450

layout(push_constant) uniform Transformation
{
  mat4 projection;
  mat4 view;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outLocalPos;

void main()
{
  outLocalPos  = inPosition;
  mat4 rotView = mat4(mat3(transformation.view)); // remove translation from the view matrix
  vec4 clipPos = transformation.projection * rotView * vec4(outLocalPos, 1.0);
  gl_Position  = clipPos.xyww;
}