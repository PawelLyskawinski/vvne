#version 450

layout(set = 0, binding = 0) uniform sampler2D h0_k_texture;
layout(set = 0, binding = 1) uniform sampler2D h0_minus_k_texture;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Time { float t; }
push_const;

const float M_PI = 3.1415926535897932384626433832795;
const int   L    = 1000; // capillar supress factor

struct complex
{
  float real;
  float im;
};

complex complex_from_vec2(vec2 value)
{
  complex c;
  c.real = value.r;
  c.im   = value.g;
  return c;
}

complex mul(complex a, complex b)
{
  complex c;
  c.real = a.real * b.real - a.im * b.im;
  c.im   = a.real * b.im + a.im * b.real;
  return c;
}

complex add(complex a, complex b)
{
  complex c;
  c.real = a.real + b.real;
  c.im   = a.im + b.im;
  return c;
}

complex conj(complex c) { return complex(c.real, -c.im); }

void main()
{
  const complex tilde_h0k          = complex_from_vec2(texture(h0_k_texture, inUV.xy).rg);
  const complex tilde_h0minuskconj = conj(complex_from_vec2(texture(h0_minus_k_texture, inUV.xy).rg));

  const vec2 k   = vec2(2.0 * M_PI * inUV.x / L, 2.0 * M_PI * inUV.y / L);
  float      mag = length(k);

  if (0.0001 > mag)
    mag = 0.0001;

  const float w       = sqrt(9.81 * mag);
  const float cosinus = cos(w * push_const.t);
  const float sinus   = sin(w * push_const.t);

  // euler formula
  const complex exp_iwt     = complex(cosinus, sinus);
  const complex exp_iwt_inv = conj(exp_iwt);

  complex dy = add(mul(tilde_h0k, exp_iwt), mul(tilde_h0minuskconj, exp_iwt_inv));

  // outColor = vec4(dy.real, dy.im, 0.0, 1.0);
  outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
