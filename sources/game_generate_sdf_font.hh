#pragma once

#include "engine/math.hh"

struct SdfChar;

struct GenerateSdfFontCommand
{
  char     character             = 0;
  uint8_t* lookup_table          = nullptr;
  SdfChar* character_data        = nullptr;
  int      characters_pool_count = 0;
  Vec2     texture_size          = {};
  float    scaling               = 0;
  Vec3     position              = {};
  float    cursor                = 0.0f;
};

struct GenerateSdfFontCommandResult
{
  Vec2   character_coordinate = {};
  Vec2   character_size       = {};
  Mat4x4 transform            = {};
  float  cursor_movement      = 0;
};

GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd);
