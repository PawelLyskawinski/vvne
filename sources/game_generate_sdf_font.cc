#include "game.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd)
{
  int idx = 0;
  for (; idx < cmd.characters_pool_count; ++idx)
    if (cmd.character == cmd.lookup_table[idx])
      break;

  SDL_assert(idx != cmd.characters_pool_count);

  const SdfChar& char_data = cmd.character_data[idx];
  auto           float_div = [](auto a, auto b) { return static_cast<float>(a) / static_cast<float>(b); };

  float width_uv_adjusted  = char_data.width / (cmd.texture_size[0] * 2.0f);
  float height_uv_adjusted = char_data.height / (cmd.texture_size[1] * 4.0f);
  float x_scaling          = cmd.scaling * width_uv_adjusted;
  float y_scaling          = cmd.scaling * height_uv_adjusted;

  float y_model_adjustment_size_factor   = y_scaling - 1.0f;
  float y_model_adjustment_offset_factor = cmd.scaling * char_data.yoffset / (cmd.texture_size[1] * 2.0f);
  float y_model_adjustment               = y_model_adjustment_offset_factor + y_model_adjustment_size_factor;

  float x_model_adjustment_size_factor   = x_scaling - 2.0f;
  float x_model_adjustment_offset_factor = cmd.scaling * char_data.xoffset / (cmd.texture_size[0] * 2.0f);
  float x_model_adjustment = cmd.cursor + x_model_adjustment_size_factor + x_model_adjustment_offset_factor;

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, x_model_adjustment + cmd.position[0], y_model_adjustment + cmd.position[1],
                   cmd.position[2]);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, x_scaling, y_scaling, 1.0f);

  GenerateSdfFontCommandResult result = {
      .character_coordinate =
          {
              float_div(char_data.x, cmd.texture_size[0]),
              float_div(char_data.y, cmd.texture_size[1]),
          },
      .character_size =
          {
              float_div(char_data.width, cmd.texture_size[0]),
              float_div(char_data.height, cmd.texture_size[1]),
          },
      .cursor_movement = cmd.scaling * float_div(char_data.xadvance, cmd.texture_size[0]),
  };

  mat4x4_mul(result.transform, translation_matrix, scale_matrix);

  return result;
}
