#pragma once

#include "engine/math.hh"

struct SdfChar;

struct GenerateSdfFontCommandResult
{
  Vec2   character_coordinate = {};
  Vec2   character_size       = {};
  Mat4x4 transform            = {};
  float  cursor_movement      = 0;
};

struct GenerateSdfFontCommand
{
  char     character             = 0;
  uint8_t* lookup_table          = nullptr;
  SdfChar* character_data        = nullptr;
  int      characters_pool_count = 0;
  Vec2     texture_size          = {};
  float    scale                 = 0.0f;
  Vec3     position              = {};
  float    cursor                = 0.0f;

  GenerateSdfFontCommandResult operator()() const;
};
