#pragma once

#include <SDL2/SDL_stdinc.h>

template <uint32_t N> struct Array
{
  float&       operator[](uint32_t i) { return data[i]; }
  const float& operator[](uint32_t i) const { return data[i]; }

  Array<N> clone() const { return *this; }

  void scale(float scale)
  {
    for (uint32_t i = 0; i < N; ++i)
      data[i] *= scale;
  }

  void subtract(const Array<N>& other)
  {
    for (uint32_t i = 0; i < N; ++i)
      data[i] -= other[i];
  }

  void clear()
  {
    for (uint32_t i = 0; i < N; ++i)
      data[i] = 0.0f;
  }

  Array<N> operator*(float scale) const
  {
    Array<N> r = clone();
    r.scale(scale);
    return r;
  }

  Array<N> operator-(const Array<N>& other) const
  {
    Array<N> r = clone();
    r.subtract(other);
    return r;
  }

  float data[N];
};

using Vec2 = Array<2>;
using Vec3 = Array<3>;
using Vec4 = Array<4>;

struct Quat : public Vec4
{
  explicit Quat(const Vec3& rhs)
      : Vec4{}
  {
    for (uint32_t i = 0; i < 3; ++i)
      data[i] = rhs[i];
  }

  static Quat rotate(const float angle_rad, const Vec3& axis)
  {
    Quat v = Quat(axis * SDL_sinf(angle_rad / 2.0f));
    v[3]   = SDL_cosf(angle_rad / 2.0f);
    return v;
  }
};

struct Mat4
{
  Mat4()
      : columns{}
  {
  }

  explicit Mat4(const Quat& quat)
      : columns{}
  {
    float a  = quat[3];
    float b  = quat[0];
    float c  = quat[1];
    float d  = quat[2];
    float a2 = a * a;
    float b2 = b * b;
    float c2 = c * c;
    float d2 = d * d;

    columns[0][0] = a2 + b2 - c2 - d2;
    columns[0][1] = 2.f * (b * c + a * d);
    columns[0][2] = 2.f * (b * d - a * c);
    columns[0][3] = 0.f;

    columns[1][0] = 2 * (b * c - a * d);
    columns[1][1] = a2 - b2 + c2 - d2;
    columns[1][2] = 2.f * (c * d + a * b);
    columns[1][3] = 0.f;

    columns[2][0] = 2.f * (b * d + a * c);
    columns[2][1] = 2.f * (c * d - a * b);
    columns[2][2] = a2 - b2 - c2 + d2;
    columns[2][3] = 0.f;

    columns[3][0] = 0.f;
    columns[3][1] = 0.f;
    columns[3][2] = 0.f;
    columns[3][3] = 1.f;
  }

  void identity()
  {
    for (auto& column : columns)
      column.clear();

    for (uint32_t row = 0; row < 4; ++row)
      for (uint32_t col = 0; col < 4; ++col)
        if (row == col)
          columns[row][col] = 1.0f;
  }

  Vec4&       operator[](uint32_t i) { return columns[i]; }
  const Vec4& operator[](uint32_t i) const { return columns[i]; }

  static Mat4 Translation(float x, float y, float z)
  {
    Mat4 r;
    r.identity();
    r[3][0] = x;
    r[3][1] = y;
    r[3][2] = z;
    return r;
  }

  static Mat4 Scale(float x, float y, float z)
  {
    Mat4 r;
    r.identity();
    r.columns[0].scale(x);
    r.columns[1].scale(y);
    r.columns[2].scale(z);
    return r;
  }

  float* data() { return columns[0].data; }

  Mat4 operator*(const Mat4 rhs)
  {
    Mat4 result;

    for (uint32_t c = 0; c < 4; ++c)
      for (uint32_t r = 0; r < 4; ++r)
      {
        result.columns[c][r] = 0.f;
        for (uint32_t k = 0; k < 4; ++k)
          result.columns[c][r] += columns[k][r] * rhs.columns[c][k];
      }

    return result;
  }

  Vec4 columns[4];
};

