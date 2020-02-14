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

GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd)
{
  const char*    begin         = reinterpret_cast<const char*>(cmd.lookup_table);
  const char*    end           = begin + cmd.characters_pool_count;
  const SdfChar& char_data     = cmd.character_data[find_index(begin, end, cmd.character)];
  const Vec2     char_size     = Vec2(char_data.width, char_data.height);
  const Vec2     char_offsets  = Vec2(char_data.xoffset, char_data.yoffset);
  const Vec2     char_position = Vec2(char_data.x, char_data.y);
  const Vec2     uv_adjusted   = char_size.scale(cmd.texture_size.invert()).scale(Vec2(0.5f, 0.25f));
  const Vec2     scaling       = uv_adjusted.scale(cmd.scaling);

  const Vec2 model_adjustment =
      scaling + char_offsets.scale(cmd.texture_size.invert()).scale(0.5f * cmd.scaling) - Vec2(2.0f - cmd.cursor, 1.0f);

  const Mat4x4 translation = Mat4x4::Translation(Vec3(model_adjustment, 0.0f) + cmd.position);
  const Mat4x4 scale       = Mat4x4::Scale(Vec3(uv_adjusted.scale(cmd.scaling), 1.0f));

  GenerateSdfFontCommandResult result = {
      .character_coordinate = char_position.scale(cmd.texture_size.invert()),
      .character_size       = char_size.scale(cmd.texture_size.invert()),
      .transform            = translation * scale,
      .cursor_movement      = cmd.scaling * (static_cast<float>(char_data.xadvance) / cmd.texture_size.x),
  };

  return result;
}
