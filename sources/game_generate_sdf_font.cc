#include "game_generate_sdf_font.hh"
#include "materials.hh"
#include <SDL2/SDL_assert.h>
#include <algorithm>

template <typename T> uint32_t find_index(const T* begin, const T* end, const T& search)
{
  auto it = std::find(begin, end, search);
  SDL_assert(end != it);
  return static_cast<uint32_t>(std::distance(begin, it));
}

GenerateSdfFontCommandResult GenerateSdfFontCommand::operator()() const
{
  const char*    begin         = reinterpret_cast<const char*>(lookup_table);
  const char*    end           = begin + characters_pool_count;
  const SdfChar& char_data     = character_data[find_index(begin, end, character)];
  const Vec2     char_size     = Vec2(char_data.width, char_data.height);
  const Vec2     char_offsets  = Vec2(char_data.xoffset, char_data.yoffset);
  const Vec2     char_position = Vec2(char_data.x, char_data.y);
  const Vec2     uv_adjusted   = char_size.scale(texture_size.invert()).scale(Vec2(0.5f, 0.25f));
  const Vec2     uv_scaled     = uv_adjusted.scale(scale);

  const Vec2 model_adjustment =
      uv_scaled + char_offsets.scale(texture_size.invert()).scale(0.5f * scale) - Vec2(2.0f - cursor, 1.0f);

  const Mat4x4 translation = Mat4x4::Translation(Vec3(model_adjustment, 0.0f) + position);
  const Mat4x4 scale_mtx   = Mat4x4::Scale(Vec3(uv_adjusted.scale(uv_scaled), 1.0f));

  GenerateSdfFontCommandResult result = {
      .character_coordinate = char_position.scale(texture_size.invert()),
      .character_size       = char_size.scale(texture_size.invert()),
      .transform            = translation * scale_mtx,
      .cursor_movement      = scale * (static_cast<float>(char_data.xadvance) / texture_size.x),
  };

  return result;
}
