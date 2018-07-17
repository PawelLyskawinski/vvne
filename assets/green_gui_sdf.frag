#version 450

layout(push_constant) uniform Transformation
{
  layout(offset = 80) vec3 color;
  float                    time;
}
transformation;

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
  float distance    = texture(image, inUV).a;
  float smoothWidth = fwidth(distance);
  float alpha       = smoothstep(0.5 - smoothWidth, 0.5 + smoothWidth, distance);
  vec3  rgb         = transformation.color;

  float time       = transformation.time;
  vec2  resolution = vec2(200, 100);
  vec2  q          = gl_FragCoord.xy / resolution.xy;
  vec2  uv         = inUV;
  vec3  col        = rgb;

  col = clamp(col * 0.5 + 0.5 * col * col * 1.2, 0.0, 1.0);
  col *= 0.5 + 0.5 * 16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
  col *= vec3(0.4, 1.0, 0.6);
  col *= 0.9 + 0.1 * sin(15.0 * -time + uv.x * 1000.0f);
  col *= 1.0 + 0.03 * sin(120.0 * time);

  outColor = vec4(rgb, alpha);
}
