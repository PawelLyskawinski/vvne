#version 450

layout(push_constant) uniform Transformation
{
  vec4 offset;
  vec4 scale;
}
transformation;

void main()
{
  const vec4 vertices[3] = vec4[3](
      vec4(-1.0,  1.0, 0.0, 1.0),
      vec4(-1.0, -1.0, 0.0, 1.0),
      vec4( 1.0,  0.0, 0.0, 1.0)
      );

  gl_Position = vertices[gl_VertexIndex] * vec4(vec2(transformation.scale.xy), 1.0, 1.0) + transformation.offset;
}