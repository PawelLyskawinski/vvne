#version 450

layout(push_constant) uniform Transformation
{
  layout(offset = 64) float time;
}
transformation;

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
  float time       = transformation.time;
  vec2  resolution = vec2(1200, 800);
  vec2  q          = gl_FragCoord.xy / resolution.xy;
  vec2  uv         = inUV;

  vec3 oricol = texture(image, vec2(q.x, 1.0 - q.y)).xyz;
  vec3 col;

  col.r = texture(image, vec2(uv.x + 0.04 * sin(time), uv.y)).x;
  col.g = texture(image, vec2(uv.x + 0.0, uv.y)).y;
  col.b = texture(image, vec2(uv.x - 0.04 * cos(time), uv.y)).z;

  col = clamp(col * 0.5 + 0.5 * col * col * 1.2, 0.0, 1.0);
  col *= 0.5 + 0.5 * 16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
  col *= vec3(0.8, 1.0, 0.7);
  col *= 0.9 + 0.1 * sin(15.0 * time + uv.y * 1000.0);
  col *= 1.0 + 0.03 * sin(120.0 * time);

  outColor = vec4(col, texture(image, inUV.st).a * 0.75);
}
