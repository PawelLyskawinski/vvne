#version 450

layout(push_constant) uniform Transformation
{
  vec4 position;
}
transformation;

void main()
{
  gl_Position = transformation.position;
  gl_PointSize = 5;
}