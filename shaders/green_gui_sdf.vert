#version 450

layout(push_constant) uniform Transformation
{
  mat4x4 mvp;
  vec2   character_coordinate;
  vec2   character_size;
}
transformation;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec2 outUV;

void main()
{
  outUV = transformation.character_coordinate + (inUV * transformation.character_size);
  vec4 position = transformation.mvp * vec4(inPosition, 0.0, 1.0);

  gl_Position = position;
  outPosition = position.xyz;
}