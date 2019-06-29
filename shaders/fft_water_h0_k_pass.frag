#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const float M_PI       = 3.1415926535897932384626433832795;
const int   L          = 1000;  // capillar supress factor
const float A          = 20.0f; // amplitude
const vec2  w          = vec2(1.0, 0.0);
const float wind_speed = 26.0f;
const float g          = 9.81;

float rand(vec2 n) { return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453); }

vec2 gauss_rng()
{
  float noise00 = mod(rand(inUV.xy), 1.0);
  float noise01 = mod(rand(inUV.yx), 1.0);
  float noise02 = mod(rand(inUV.xx), 1.0);
  float noise03 = mod(rand(inUV.yy), 1.0);

  float u0 = 2.0 * M_PI * noise00;
  float v0 = sqrt(-2.0 * log(noise01));
  float u1 = 2.0 * M_PI * noise02;
  float v1 = sqrt(-2.0 * log(noise03));

  return vec2(v0 * cos(u0), v0 * sin(u0));
}

void main()
{
  vec2  x   = inUV.xy;
  vec2  k   = vec2(2.0 * M_PI * x.x / L, 2.0 * M_PI * x.y / L);
  float L_  = (wind_speed * wind_speed) / g;
  float mag = length(k);

  if (0.0001 > mag)
    mag = 0.0001;

  float mag_squered = mag * mag;

  float h0k = clamp(sqrt(                                           //
                        (A / (mag_squered * mag_squered))           //
                        * pow(dot(normalize(k), normalize(w)), 4.0) //
                        * exp(-(1.0 / (mag_squered * L_ * L_)))     //
                        * exp(-mag_squered * pow(L / 2000.0, 2)))   //
                        / sqrt(2.0),
                    -4000.0, 4000.0);

  vec2 gauss_random = gauss_rng();

  outColor = vec4(gauss_random.xy * h0k, 0.0, 1.0);
}
