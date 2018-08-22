#version 450

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Transformation
{
  mat4 lightSpaceMatrix;
  mat4 model;
}
transformation;

void main()
{
  gl_Position = transformation.lightSpaceMatrix * transformation.model * vec4(inPos, 1.0);
}