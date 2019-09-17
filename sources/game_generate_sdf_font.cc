#include "game.hh"
#include <SDL2/SDL_assert.h>
#include <algorithm>

static uint32_t find_character_index(const uint8_t* begin, const uint8_t* end, const uint8_t search)
{
  auto it = std::find(begin, end, search);
  SDL_assert(end != it);
  return static_cast<uint32_t>(std::distance(begin, it));
}

GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd)
{
  const uint8_t* begin     = cmd.lookup_table;
  const uint8_t* end       = begin + cmd.characters_pool_count;
  const SdfChar& char_data = cmd.character_data[find_character_index(begin, end, cmd.character)];

  const Vec2  char_size          = Vec2(char_data.width, char_data.height);

  float width_uv_adjusted  = char_data.width / (cmd.texture_size.x * 2.0f);
  float height_uv_adjusted = char_data.height / (cmd.texture_size.y * 4.0f);

  float x_scaling = cmd.scaling * width_uv_adjusted;
  float y_scaling = cmd.scaling * height_uv_adjusted;

  float y_model_adjustment_size_factor   = y_scaling - 1.0f;
  float y_model_adjustment_offset_factor = cmd.scaling * char_data.yoffset / (cmd.texture_size.y * 2.0f);
  float y_model_adjustment               = y_model_adjustment_offset_factor + y_model_adjustment_size_factor;

  float x_model_adjustment_size_factor   = x_scaling - 2.0f;
  float x_model_adjustment_offset_factor = cmd.scaling * char_data.xoffset / (cmd.texture_size.x * 2.0f);
  float x_model_adjustment = cmd.cursor + x_model_adjustment_size_factor + x_model_adjustment_offset_factor;

  Mat4x4 translation_matrix;
  translation_matrix.translate(
      Vec3(x_model_adjustment + cmd.position.x, y_model_adjustment + cmd.position.y, cmd.position.z));

  Mat4x4 scale_matrix = Mat4x4::Scale(Vec3(x_scaling, y_scaling, 1.0f));

  GenerateSdfFontCommandResult result = {
      .character_coordinate =
          {
              static_cast<float>(char_data.x) / cmd.texture_size.x,
              static_cast<float>(char_data.y) / cmd.texture_size.y,
          },
      .character_size =
          {
              static_cast<float>(char_data.width) / cmd.texture_size.x,
              static_cast<float>(char_data.height) / cmd.texture_size.y,
          },
      .cursor_movement = cmd.scaling * (static_cast<float>(char_data.xadvance) / cmd.texture_size.x),
  };

  result.transform = translation_matrix * scale_matrix;

  return result;
}
