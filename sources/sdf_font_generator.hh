#pragma once

#include "engine/math.hh"

struct SdfChar;

struct RenderableChar
{
  Vec2   character_coordinate = {};
  Vec2   character_size       = {};
  Mat4x4 transform            = {};
  Vec2   cursor_movement      = {};
};

struct SdfFontGenerator
{
  char     character             = 0;
  uint8_t* lookup_table          = nullptr;
  SdfChar* character_data        = nullptr;
  int      characters_pool_count = 0;
  Vec2     texture_size          = {};
  float    rescaling             = 0;
  Vec3     position              = {};
  Vec2     cursor                = {};

  RenderableChar operator()();
  RenderableChar calculate() const;
};
