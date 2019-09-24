#include "game.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <algorithm>

static uint32_t find_character_index(const uint8_t* begin, const uint8_t* end, const uint8_t search)
{
  auto it = std::find(begin, end, search);
  SDL_assert(end != it);
  return static_cast<uint32_t>(std::distance(begin, it));
}

GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd)
{
  const uint8_t* begin                 = cmd.lookup_table;
  const uint8_t* end                   = begin + cmd.characters_pool_count;
  const SdfChar& char_data             = cmd.character_data[find_character_index(begin, end, cmd.character)];
  const Vec2     char_size             = Vec2(char_data.width, char_data.height);
  const Vec2     char_offsets          = Vec2(char_data.xoffset, char_data.yoffset);
  const Vec2     char_position         = Vec2(char_data.x, char_data.y);
  const Vec2     uv_adjusted           = char_size.scale(cmd.texture_size).scale(Vec2(0.5f, 0.25f));
  const Vec2     scaling               = uv_adjusted.scale(cmd.scaling);
  const Vec2     inverted_texture_size = cmd.texture_size.invert();

  const Vec2 model_adjustment =
      scaling + (char_offsets.scale(cmd.scaling).scale(cmd.texture_size.scale(2.0f))) - Vec2(2.0f + cmd.cursor, 1.0f);

  const Mat4x4 translation = Mat4x4::Translation(Vec3(model_adjustment, 0.0f) + cmd.position);
  const Mat4x4 scale       = Mat4x4::Scale(Vec3(uv_adjusted.scale(cmd.scaling), 1.0f));

  GenerateSdfFontCommandResult result = {
      .character_coordinate = char_position.scale(inverted_texture_size),
      .character_size       = char_size.scale(inverted_texture_size),
      .transform            = translation * scale,
      .cursor_movement      = cmd.scaling * (static_cast<float>(char_data.xadvance) / cmd.texture_size.x),
  };

  return result;
}
