#pragma once

#include <SDL2/SDL_stdinc.h>

template <uint32_t DIM> struct Vec
{
  Vec<DIM>() = default;

  float&       operator[](uint32_t i) { return data[i]; }
  const float& operator[](uint32_t i) const { return data[i]; }

  Vec<DIM> operator+(const Vec<DIM>& rhs) const
  {
    Vec<DIM> r;
    for (uint32_t i = 0; i < DIM; ++i)
      r[i] = data[i] + rhs[i];
    return r;
  }

  Vec<DIM> operator-(const Vec<DIM>& rhs) const
  {
    Vec<DIM> r;
    for (uint32_t i = 0; i < DIM; ++i)
      r[i] = data[i] - rhs[i];
    return r;
  }

  Vec<DIM>& scale(float s)
  {
    for (uint32_t i = 0; i < DIM; ++i)
      data[i] *= s;
    return *this;
  }

  Vec<DIM> mul_cross(const Vec<DIM>& rhs)
  {
    // @todo: make it work for anything other than DIM=3
    Vec<DIM> r;
    r[0] = data[1] * rhs[2] - data[2] * rhs[1];
    r[1] = data[2] * rhs[0] - data[0] * rhs[2];
    r[2] = data[0] * rhs[1] - data[1] * rhs[0];
    return r;
  }

  float mul_inner(const Vec<DIM>& rhs)
  {
    float p = 0.0f;
    for (uint32_t i = 0; i < DIM; ++i)
      p += data[i] * rhs[i];
    return p;
  }

  Vec<DIM> lerp(const Vec<DIM>& b, float t)
  {
    Vec<DIM> r;
    for (int i = 0; i < DIM; ++i)
    {
      float distance = b[i] - data[i];
      float progress = distance * t;
      r[i]           = data[i] + progress;
    }
    return r;
  }

  float data[DIM] = {};
};

using Vec4 = Vec<4>;
using Vec3 = Vec<3>;
using Vec2 = Vec<2>;

struct Quaternion
{
  Quaternion() = default;

  Quaternion& rotate(Vec3 axis, float angle)
  {
    axis.scale(SDL_sinf(angle / 2));
    a = axis[0];
    b = axis[1];
    c = axis[2];
    d = SDL_cosf(angle / 2);
    return *this;
  }

  Vec3 as_vec3() const { return {a, b, c}; }

  Quaternion operator*(const Quaternion& rhs)
  {
    Vec3 r = as_vec3().mul_cross(rhs.as_vec3()) + as_vec3().scale(rhs.d) + rhs.as_vec3().scale(d);
    return {r[0], r[1], r[2], (d * rhs.d) - as_vec3().mul_inner(rhs.as_vec3())};
  }

  Quaternion& rotate_x(float angle) { return rotate({1.0f, 0.0f, 0.0f}, angle); }
  Quaternion& rotate_y(float angle) { return rotate({0.0f, 1.0f, 0.0f}, angle); }
  Quaternion& rotate_z(float angle) { return rotate({0.0f, 0.0f, 1.0f}, angle); }

  float a = 0.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
};

struct Mat4
{
  Mat4() = default;

  explicit Mat4(const Quaternion& q)
  {
    float a = q.d;
    float b = q.a;
    float c = q.b;
    float d = q.c;

    float a2 = a * a;
    float b2 = b * b;
    float c2 = c * c;
    float d2 = d * d;

    M[0][0] = a2 + b2 - c2 - d2;
    M[0][1] = 2.0f * (b * c + a * d);
    M[0][2] = 2.0f * (b * d - a * c);
    M[0][3] = 0.0f;

    M[1][0] = 2 * (b * c - a * d);
    M[1][1] = a2 - b2 + c2 - d2;
    M[1][2] = 2.0f * (c * d + a * b);
    M[1][3] = 0.0f;

    M[2][0] = 2.0f * (b * d + a * c);
    M[2][1] = 2.0f * (c * d - a * b);
    M[2][2] = a2 - b2 - c2 + d2;
    M[2][3] = 0.0f;

    M[3][0] = 0.0f;
    M[3][1] = 0.0f;
    M[3][2] = 0.0f;
    M[3][3] = 1.0f;
  }

  Mat4& translate(const Vec3& in)
  {
    identity();
    M[3][0] = in[0];
    M[3][1] = in[1];
    M[3][2] = in[2];
    return *this;
  }

  Mat4& identity()
  {
    for (uint32_t i = 0; i < 4; ++i)
      for (uint32_t j = 0; j < 4; ++j)
        M[i][j] = (i == j) ? 1.f : 0.f;
    return *this;
  }

  Mat4& scale(float x, float y, float z)
  {
    M[0].scale(x);
    M[1].scale(y);
    M[2].scale(z);
    return *this;
  }

  Mat4& scale(float val) { return scale(val, val, val); }

  Mat4 operator*(const Mat4& rhs)
  {
    Mat4 result;
    for (uint32_t c = 0; c < 4; ++c)
      for (uint32_t r = 0; r < 4; ++r)
        for (uint32_t k = 0; k < 4; ++k)
          result.M[c][r] += M[k][r] * rhs.M[c][k];
    return result;
  }

  float* data() { return M[0].data; }

  Vec4 M[4] = {};
};