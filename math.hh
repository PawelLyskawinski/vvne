#pragma once

#include <SDL2/SDL_stdinc.h>

struct Vec3
{
  Vec3() = default;

  explicit Vec3(float val)
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

  Vec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
  Vec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};
