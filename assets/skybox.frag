#version 450

layout(binding = 0) uniform sampler2D equirectangularMap;
layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 outColor;

const vec2 invAtan = vec2(0.1591, 0.3183);
vec2 sampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

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
  vec2 uv = sampleSphericalMap(normalize(inUVW));
  vec3 color = texture(equirectangularMap, uv).rgb;

  float exposure = 1.2;
  float gamma = 1.5;

  // Tone mapping
  color = tonemap(color * exposure);
  color = color * (1.0f / tonemap(vec3(11.2f)));

  // Gamma correction
  color    = pow(color, vec3(1.0f / gamma));
  outColor = vec4(color * 1.0, 1.0);
}
