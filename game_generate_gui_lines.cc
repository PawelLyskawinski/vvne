#include "game.hh"

namespace {

uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

} // namespace

void generate_gui_lines(const GenerateGuiLinesCommand& cmd, GuiLine* dst, int* count)
{
  if (nullptr == dst)
  {
    *count = 64;
    return;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Main green rulers
  //////////////////////////////////////////////////////////////////////////////

  const float width               = 0.75f;
  const float height              = 1.0f;
  const float offset_up           = 0.2f;
  const float ruler_lid_length    = 0.05f;
  const float vertical_correction = 0.008f;
  const float tiny_line_offset    = 0.011f;
  const float max_left_x          = -0.5f * width;
  const float max_right_x         = -max_left_x;
  const float top_y               = -0.5f * height - offset_up;
  const float bottom_y            = 0.5f * height - offset_up;

  dst[0] = {
      {max_left_x, top_y},
      {max_left_x + ruler_lid_length, top_y},
      GuiLine::Size::Big,
      GuiLine::Color::Green,
  };

  dst[1] = {
      {max_left_x + ruler_lid_length, top_y - vertical_correction},
      {max_left_x + ruler_lid_length, bottom_y + vertical_correction},
      GuiLine::Size::Small,
      GuiLine::Color::Green,
  };

  dst[2] = {
      {max_left_x + ruler_lid_length - tiny_line_offset, top_y - vertical_correction},
      {max_left_x + ruler_lid_length - tiny_line_offset, bottom_y + vertical_correction},
      GuiLine::Size::Tiny,
      GuiLine::Color::Green,
  };

  dst[3] = {
      {max_left_x, bottom_y},
      {max_left_x + ruler_lid_length, bottom_y},
      GuiLine::Size::Big,
      GuiLine::Color::Green,
  };

  dst[4] = {
      {max_right_x - ruler_lid_length, top_y},
      {max_right_x, top_y},
      GuiLine::Size::Big,
      GuiLine::Color::Green,
  };

  dst[5] = {
      {max_right_x - ruler_lid_length, top_y - vertical_correction},
      {max_right_x - ruler_lid_length, bottom_y + vertical_correction},
      GuiLine::Size::Small,
      GuiLine::Color::Green,
  };

  dst[6] = {
      {max_right_x - ruler_lid_length + tiny_line_offset, top_y - vertical_correction},
      {max_right_x - ruler_lid_length + tiny_line_offset, bottom_y + vertical_correction},
      GuiLine::Size::Tiny,
      GuiLine::Color::Green,
  };

  dst[7] = {
      {max_right_x, bottom_y},
      {max_right_x - ruler_lid_length, bottom_y},
      GuiLine::Size::Big,
      GuiLine::Color::Green,
  };

  //////////////////////////////////////////////////////////////////////////////
  /// Detail on left green ruler
  //////////////////////////////////////////////////////////////////////////////

  for (int i = 0; i < 25; ++i)
  {
    const float offset       = 0.04f;
    const float big_indent   = 0.025f;
    const float small_indent = 0.01f;
    GuiLine&    line         = dst[8 + i];

    line = {
        {max_left_x + big_indent, top_y + (i * offset)},
        {max_left_x + ruler_lid_length - tiny_line_offset, top_y + (i * offset)},
        GuiLine::Size::Small,
        GuiLine::Color::Green,
    };

    if (0 == ((i + 2) % 5))
      line.a[0] = max_left_x + small_indent;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Red height rulers
  //////////////////////////////////////////////////////////////////////////////

  const float red_x_offset                 = 0.02f;
  const float height_ruler_length          = 0.04f;
  const float height_ruler_left_x_position = max_left_x + ruler_lid_length + red_x_offset;

  for (int side = 0; side < 2; ++side)
  {
    for (int i = 0; i < 4; ++i)
    {
      GuiLine*    line_stride = &dst[32 + 3 * ((4 * side) + i)];
      const float side_mod    = (0 < side) ? -1.0f : 1.0f;
      const vec2 base_offset = {side_mod * height_ruler_left_x_position, -1.2f - (cmd.player_y_location_meters / 8.0f)};
      const vec2 size        = {side_mod * height_ruler_length, 0.2f};

      vec2 offset     = {};
      vec2 correction = {0.0f, i * 0.4f};
      vec2_add(offset, correction, base_offset);

      line_stride[0] = {
          {offset[0], offset[1] + size[1] / 2.0f},
          {offset[0] + size[0], offset[1] + size[1] / 2.0f},
          GuiLine::Size::Tiny,
          GuiLine::Color::Red,
      };

      line_stride[1] = {
          {offset[0], offset[1] + size[1] / 2.0f},
          {offset[0], offset[1] - size[1] / 2.0f},
          GuiLine::Size::Tiny,
          GuiLine::Color::Red,
      };

      line_stride[2] = {
          {offset[0], offset[1] - size[1] / 2.0f},
          {offset[0] + size[0], offset[1] - size[1] / 2.0f},
          GuiLine::Size::Tiny,
          GuiLine::Color::Red,
      };
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Yellow pitch rulers
  //////////////////////////////////////////////////////////////////////////////

  // :: 56 -> 64
  for (int i = 0; i < 7; ++i)
  {
    const float distance_from_main = 0.16f;
    const float horizontal_offset  = 0.4f;

    const float x_left  = (max_left_x + ruler_lid_length + distance_from_main);
    const float x_right = -x_left;
    const float y       = -offset_up + (i * horizontal_offset) - (2 * horizontal_offset) + cmd.camera_y_pitch_radians;

    float rotation_matrix[] = {SDL_cosf(cmd.camera_x_pitch_radians), -1.0f * SDL_sinf(cmd.camera_x_pitch_radians),
                               SDL_sinf(cmd.camera_x_pitch_radians), SDL_cosf(cmd.camera_x_pitch_radians)};

    dst[56 + i] = {
        {x_left * rotation_matrix[0] + y * rotation_matrix[2], x_left * rotation_matrix[1] + y * rotation_matrix[3]},
        {x_right * rotation_matrix[0] + y * rotation_matrix[2], x_right * rotation_matrix[1] + y * rotation_matrix[3]},
        GuiLine::Size::Small,
        GuiLine::Color::Yellow,
    };
  }
}

void generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count)
{
  if (nullptr == dst)
  {
    *count = 12;
    return;
  }

  float y_zeroed      = line_to_pixel_length(0.88f - (cmd.player_y_location_meters / 8.0f), cmd.screen_extent2D.height);
  float y_step        = line_to_pixel_length(0.2f, cmd.screen_extent2D.height);
  float x_offset_left = line_to_pixel_length(0.74f, cmd.screen_extent2D.width);
  float x_offset_right = x_offset_left + line_to_pixel_length(0.51f, cmd.screen_extent2D.width);
  int   size           = line_to_pixel_length(0.5f, cmd.screen_extent2D.height);

  // -------- left side --------
  for (int i = 0; i < 6; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);

    dst[i].offset[0] = x_offset_left;
    dst[i].offset[1] = y_zeroed + (y_step_modifier * y_step);
    dst[i].value     = -5 * y_step_modifier;
    dst[i].size      = size;
  }

  // -------- right side --------
  for (int i = 0; i < 6; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);
    int idx             = 6 + i;
    int value           = -5 * y_step_modifier;

    float additional_character_offset = ((SDL_abs(value) > 9) ? 6.0f : 0.0f) + ((value < 0) ? 6.8f : 0.0f);

    dst[idx].offset[0] = x_offset_right - additional_character_offset;
    dst[idx].offset[1] = y_zeroed + (y_step_modifier * y_step);
    dst[idx].value     = value;
    dst[idx].size      = size;
  }
}

void generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count)
{
  if (nullptr == dst)
  {
    *count = 7;
    return;
  }

  float start_x_offset           = line_to_pixel_length(1.18f, cmd.screen_extent2D.width);
  float start_y_offset           = line_to_pixel_length(1.58f, cmd.screen_extent2D.height);
  float y_distance_between_lines = line_to_pixel_length(0.4f, cmd.screen_extent2D.height);
  float y_pitch_modifier         = line_to_pixel_length(1.0f, cmd.screen_extent2D.height);
  int   step_between_lines       = 10;
  int   size                     = line_to_pixel_length(0.6f, cmd.screen_extent2D.height);

  for (int i = 0; i < 7; ++i)
  {
    dst[i].offset[0] = start_x_offset;
    dst[i].offset[1] =
        start_y_offset + ((2 - i) * y_distance_between_lines) + (y_pitch_modifier * cmd.camera_y_pitch_radians);
    dst[i].value = SDL_abs((4 - i) * step_between_lines);
    dst[i].size  = size;
  }
}
