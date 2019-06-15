#pragma once

#include <SDL2/SDL_stdinc.h>
#include <linmath.h>
#include <vulkan/vulkan.h>

constexpr float                   to_rad(float deg) noexcept { return (float(M_PI) * deg) / 180.0f; }
constexpr float                   to_deg(float rad) noexcept { return (180.0f * rad) / float(M_PI); }
template <typename T> constexpr T clamp(T val, T min, T max) { return (val < min) ? min : (val > max) ? max : val; }

struct Vec2
{
  Vec2(float x, float y)
      : x(x)
      , y(y)
  {
  }

  Vec2 operator-(const Vec2& rhs) const { return Vec2(x - rhs.x, y - rhs.y); }

  float x;
  float y;
};

struct Vec3
{
  explicit Vec3(float val = 0.0f)
      : x(val)
      , y(val)
      , z(val)
  {
  }

  Vec3(float x, float y, float z)
      : x(x)
      , y(y)
      , z(z)
  {
  }

  Vec3 operator-(const Vec3& rhs) const { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
  Vec3 operator+(const Vec3& rhs) const { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
  Vec3 scale(float s) const { return Vec3(x * s, y * s, z * s); }

  void operator+=(const Vec3& rhs)
  {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
  }

  float len() const { return SDL_sqrtf(x * x + y * y + z * z); }
  Vec3  invert_signs() const { return Vec3(-x, -y, -z); }

  void clamp(float min, float max)
  {
    x = ::clamp(x, min, max);
    y = ::clamp(y, min, max);
    z = ::clamp(z, min, max);
  }

  Vec2 xz() const { return Vec2(x, z); }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Vec4
{
  Vec4() = default;

  Vec4(float x, float y, float z, float w)
      : x(x)
      , y(y)
      , z(z)
      , w(w)
  {
  }

  Vec4(const Vec3& v, float w = 0.0f)
      : x(v.x)
      , y(v.y)
      , z(v.z)
      , w(w)
  {
  }

  Vec4  scale(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
  Vec3& as_vec3() { return *reinterpret_cast<Vec3*>(this); }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

struct Mat4x4
{
  Mat4x4() = default;
  explicit Mat4x4(mat4x4 input) { mat4x4_dup(mtx, input); }

  void translate(const Vec3 v) { mat4x4_translate(mtx, v.x, v.y, v.z); }
  void identity() { mat4x4_identity(mtx); }
  void scale(const Vec3 s) { mat4x4_scale_aniso(mtx, mtx, s.x, s.y, s.z); }

  Mat4x4 operator*(Mat4x4& rhs)
  {
    Mat4x4 r;
    mat4x4_mul(r.mtx, mtx, rhs.mtx);
    return r;
  }

  Vec4 operator*(Vec4& rhs)
  {
    Vec4 r;
    mat4x4_mul_vec4(&r.x, mtx, &rhs.x);
    return r;
  }

  void perspective(const uint32_t width, const uint32_t height, float fov_rads, float near_clipping_plane,
                   float far_clipping_plane)
  {
    float extent_width  = static_cast<float>(width);
    float extent_height = static_cast<float>(height);
    float aspect_ratio  = extent_width / extent_height;
    mat4x4_perspective(mtx, fov_rads, aspect_ratio, near_clipping_plane, far_clipping_plane);
    mtx[1][1] *= -1.0f;
  }

  void perspective(const VkExtent2D& extent, float fov_rads, float near_cp, float far_cp)
  {
    perspective(extent.width, extent.height, fov_rads, near_cp, far_cp);
  }

  void look_at(Vec3& eye, Vec3& center, Vec3& up) { mat4x4_look_at(mtx, &eye.x, &center.x, &up.x); }

  void ortho(float l, float r, float b, float t, float n, float f)
  {
    mat4x4_ortho(mtx, l, r, b, t, n, f);
    mtx[1][1] *= -1.0f;
  }

  Mat4x4 invert()
  {
    Mat4x4 r;
    mat4x4_invert(r.mtx, mtx);
    return r;
  }

  const float& at(uint32_t r, uint32_t c) const { return mtx[r][c]; }

  mat4x4 mtx = {};
};
