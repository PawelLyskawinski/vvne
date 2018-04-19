#version 450

layout(binding = 0) uniform samplerCube cube_map;
layout(location = 0) in vec3 inLocalPos;
layout(location = 0) out vec4 outColor;

vec3 tonemap(vec3 color)
{
  float A = 0.15;
  float B = 0.50;
  float C = 0.10;
  float D = 0.20;
  float E = 0.02;
  float F = 0.30;
  float W = 11.2;
  return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

void main()
{
  vec3 color = texture(cube_map, inLocalPos).rgb;

  // Tone mapping
  // color = tonemap(color * exposure);
  // color = color * (1.0f / tonemap(vec3(11.2f)));

  // Gamma correction
  // color    = pow(color, vec3(1.0f / gamma));

  // color = color / (color + vec3(1.0));
  // color = pow(color, vec3(1.0 / 2.2));

  outColor = vec4(color, 1.0);
}
