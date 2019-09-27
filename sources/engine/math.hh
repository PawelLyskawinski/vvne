#pragma once

#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 (0.5f * M_PI)
#endif

constexpr float to_rad(float deg) noexcept { return (float(M_PI) * deg) / 180.0f; }
constexpr float to_deg(float rad) noexcept { return (180.0f * rad) / float(M_PI); }

template <typename T> constexpr T clamp(T val, T min, T max) { return (val < min) ? min : (val > max) ? max : val; }

struct Vec2
{
  Vec2() = default;
  Vec2(float x, float y);
  [[nodiscard]] Vec2  operator-(const Vec2& rhs) const;
  [[nodiscard]] Vec2  operator+(const Vec2& rhs) const;
  [[nodiscard]] Vec2  scale(float s) const;
  [[nodiscard]] Vec2  scale(const Vec2& s) const;
  [[nodiscard]] float len() const;
  [[nodiscard]] Vec2  normalize() const;
  [[nodiscard]] Vec2  invert() const;

  float x = 0.0f;
  float y = 0.0f;
};

struct Vec3
{
  Vec3() = default;
  explicit Vec3(float val);
  Vec3(const Vec2& vec, float val);
  Vec3(float x, float y, float z);

  [[nodiscard]] Vec3         operator-(const Vec3& rhs) const;
  [[nodiscard]] Vec3         operator+(const Vec3& rhs) const;
  [[nodiscard]] Vec3         scale(float s) const;
  [[nodiscard]] float        len() const;
  [[nodiscard]] Vec3         invert_signs() const;
  [[nodiscard]] Vec3         normalize() const;
  [[nodiscard]] Vec2         xz() const;
  [[nodiscard]] Vec3         lerp(const Vec3& dst, float t) const;
  [[nodiscard]] Vec3         mul_cross(const Vec3& rhs) const;
  [[nodiscard]] float        mul_inner(const Vec3& rhs) const;
  [[nodiscard]] const float* data() const { return &x; }

  void operator+=(const Vec3& rhs);
  void clamp(float min, float max);

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Vec4
{
  Vec4() = default;
  Vec4(float x, float y, float z, float w);
  explicit Vec4(const Vec3& v, float w = 0.0f);

  [[nodiscard]] inline Vec4         scale(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
  [[nodiscard]] inline Vec3&        as_vec3() { return *reinterpret_cast<Vec3*>(this); }
  [[nodiscard]] inline const Vec3&  as_vec3() const { return *reinterpret_cast<const Vec3*>(this); }
  [[nodiscard]] inline float&       operator[](uint32_t i) { return *(&x + i); }
  [[nodiscard]] inline const float& operator[](uint32_t i) const { return *(&x + i); }
  [[nodiscard]] float               mul_inner(const Vec4& rhs) const;
  [[nodiscard]] Vec4                lerp(const Vec4& dst, float t) const;
  [[nodiscard]] float               len() const;
  [[nodiscard]] Vec4                normalize() const;
  [[nodiscard]] inline const float* data() const { return reinterpret_cast<const float*>(&x); }

  inline Vec4& operator+=(const Vec4& rhs)
  {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

struct Quaternion
{
  Quaternion() = default;
  Quaternion(float angle, const Vec3& axis);

  [[nodiscard]] Quaternion operator*(const Quaternion& rhs) const;
  void                     rotate(float angle, const Vec3& axis);

  Vec4 data;
};

struct Mat4x4
{
  Mat4x4() = default;
  explicit Mat4x4(const float* data);
  explicit Mat4x4(const Quaternion& quat);

  [[nodiscard]] Mat4x4              operator*(const Mat4x4& rhs) const;
  [[nodiscard]] Vec4                operator*(const Vec4& rhs) const;
  [[nodiscard]] inline const float& at(uint32_t r, uint32_t c) const { return columns[c][r]; }
  [[nodiscard]] Mat4x4              invert() const;
  [[nodiscard]] Vec4                row(uint32_t i) const;
  [[nodiscard]] inline const float* data() const { return reinterpret_cast<const float*>(&columns[0].x); };

  void perspective(uint32_t width, uint32_t height, float fov_rads, float near_clipping_plane,
                   float far_clipping_plane);
  void perspective(const VkExtent2D& extent, float fov_rads, float near_cp, float far_cp);
  void perspective(float aspect_ratio, float fov_rads, float near_cp, float far_cp);

  void ortho(float l, float r, float b, float t, float n, float f);
  void set_diagonal(const Vec3& values);

  void identity();
  void transpose();
  void translate(Vec3 v);
  void translate_in_place(const Vec3& v);
  void scale(Vec3 s);

  [[nodiscard]] static Mat4x4 RotationX(float r);
  [[nodiscard]] static Mat4x4 RotationY(float r);
  [[nodiscard]] static Mat4x4 RotationZ(float r);
  [[nodiscard]] static Mat4x4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);
  [[nodiscard]] static Mat4x4 Translation(const Vec3& t);
  [[nodiscard]] static Mat4x4 Scale(const Vec3& s);

  //
  // SPIR-V specification 2.18.1. Memory Layout
  //
  // in a matrix, lower-numbered columns appear at smaller offsets than higher-numbered columns,
  // and lower-numbered components within the matrix’s vectors appearing at smaller offsets than high-numbered c
  // mponents,
  //
  // Column-major layout maps to mathematical view:
  // [0].x [1].x [2].x [3].x
  // [0].y [1].y [2].y [3].y
  // [0].z [1].z [2].z [3].z
  // [0].w [1].w [2].w [3].w
  //
  // And in memory:
  // [0]{x, y, z, w} [1]{x, y, z, w} [2]{x, y, z, w} [3]{x, y, z, w}
  //
  Vec4 columns[4] = {};
};
