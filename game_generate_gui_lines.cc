#include "game.hh"

namespace {

uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

} // namespace

ArrayView<GuiLine> generate_gui_lines(const GenerateGuiLinesCommand& cmd)
{
  ArrayView<GuiLine> r{};
  r.count       = 64;
  r.data        = cmd.allocator->allocate_back<GuiLine>(r.count);
  GuiLine* line = r.data;

  //////////////////////////////////////////////////////////////////////////////
  // Main green rulers
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

  // :: 0
  line->a[0]  = max_left_x;
  line->a[1]  = top_y;
  line->b[0]  = max_left_x + ruler_lid_length;
  line->b[1]  = top_y;
  line->size  = GuiLine::Size::Big;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 1
  line->a[0]  = max_left_x + ruler_lid_length;
  line->a[1]  = top_y - vertical_correction;
  line->b[0]  = max_left_x + ruler_lid_length;
  line->b[1]  = bottom_y + vertical_correction;
  line->size  = GuiLine::Size::Small;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 2
  line->a[0]  = max_left_x + ruler_lid_length - tiny_line_offset;
  line->a[1]  = top_y - vertical_correction;
  line->b[0]  = max_left_x + ruler_lid_length - tiny_line_offset;
  line->b[1]  = bottom_y + vertical_correction;
  line->size  = GuiLine::Size::Tiny;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 3
  line->a[0]  = max_left_x;
  line->a[1]  = bottom_y;
  line->b[0]  = max_left_x + ruler_lid_length;
  line->b[1]  = bottom_y;
  line->size  = GuiLine::Size::Big;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 4
  line->a[0]  = max_right_x - ruler_lid_length;
  line->a[1]  = top_y;
  line->b[0]  = max_right_x;
  line->b[1]  = top_y;
  line->size  = GuiLine::Size::Big;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 5
  line->a[0]  = max_right_x - ruler_lid_length;
  line->a[1]  = top_y - vertical_correction;
  line->b[0]  = max_right_x - ruler_lid_length;
  line->b[1]  = bottom_y + vertical_correction;
  line->size  = GuiLine::Size::Small;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 6
  line->a[0]  = max_right_x - ruler_lid_length + tiny_line_offset;
  line->a[1]  = top_y - vertical_correction;
  line->b[0]  = max_right_x - ruler_lid_length + tiny_line_offset;
  line->b[1]  = bottom_y + vertical_correction;
  line->size  = GuiLine::Size::Tiny;
  line->color = GuiLine::Color::Green;
  line++;

  // :: 7
  line->a[0]  = max_right_x;
  line->a[1]  = bottom_y;
  line->b[0]  = max_right_x - ruler_lid_length;
  line->b[1]  = bottom_y;
  line->size  = GuiLine::Size::Big;
  line->color = GuiLine::Color::Green;
  line++;

  //////////////////////////////////////////////////////////////////////////////
  // Detail on left green ruler
  //////////////////////////////////////////////////////////////////////////////

  // :: 7 -> 32
  for (int i = 0; i < 25; ++i)
  {
    const float offset       = 0.04f;
    const float big_indent   = 0.025f;
    const float small_indent = 0.01f;

    line->a[0]  = max_left_x + big_indent;
    line->a[1]  = top_y + (i * offset);
    line->b[0]  = max_left_x + ruler_lid_length - tiny_line_offset;
    line->b[1]  = top_y + (i * offset);
    line->size  = GuiLine::Size::Small;
    line->color = GuiLine::Color::Green;

    if (0 == ((i + 2) % 5))
      line->a[0] = max_left_x + small_indent;

    line++;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Red height rulers
  //////////////////////////////////////////////////////////////////////////////

  const float red_x_offset                 = 0.02f;
  const float height_ruler_length          = 0.04f;
  const float height_ruler_left_x_position = max_left_x + ruler_lid_length + red_x_offset;

  // :: 32 -> 56
  for (int side = 0; side < 2; ++side)
  {
    for (int i = 0; i < 4; ++i)
    {
      const float side_mod   = side ? -1.0f : 1.0f;
      const vec2 base_offset = {side_mod * height_ruler_left_x_position, -1.2f - (cmd.player_y_location_meters / 8.0f)};
      const vec2 size        = {side_mod * height_ruler_length, 0.2f};

      vec2 offset     = {};
      vec2 correction = {0.0f, i * 0.4f};
      vec2_add(offset, correction, base_offset);

      // Top
      line->a[0]  = offset[0];
      line->a[1]  = offset[1] + size[1] / 2.0f;
      line->b[0]  = offset[0] + size[0];
      line->b[1]  = offset[1] + size[1] / 2.0f;
      line->size  = GuiLine::Size::Tiny;
      line->color = GuiLine::Color::Red;
      line++;

      // Mid
      line->a[0]  = offset[0];
      line->a[1]  = offset[1] + size[1] / 2.0f;
      line->b[0]  = offset[0];
      line->b[1]  = offset[1] - size[1] / 2.0f;
      line->size  = GuiLine::Size::Tiny;
      line->color = GuiLine::Color::Red;
      line++;

      // Bottom
      line->a[0]  = offset[0];
      line->a[1]  = offset[1] - size[1] / 2.0f;
      line->b[0]  = offset[0] + size[0];
      line->b[1]  = offset[1] - size[1] / 2.0f;
      line->size  = GuiLine::Size::Tiny;
      line->color = GuiLine::Color::Red;
      line++;
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Yellow pitch rulers
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

    line->a[0]  = x_left * rotation_matrix[0] + y * rotation_matrix[2];
    line->a[1]  = x_left * rotation_matrix[1] + y * rotation_matrix[3];
    line->b[0]  = x_right * rotation_matrix[0] + y * rotation_matrix[2];
    line->b[1]  = x_right * rotation_matrix[1] + y * rotation_matrix[3];
    line->size  = GuiLine::Size::Small;
    line->color = GuiLine::Color::Yellow;
    line++;
  }

  return r;
}

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd)
{
  ArrayView<GuiHeightRulerText> r{};
  r.count = 12;
  r.data  = cmd.allocator->allocate_back<GuiHeightRulerText>(r.count);

  float y_zeroed      = line_to_pixel_length(0.88f - (cmd.player_y_location_meters / 8.0f), cmd.screen_extent2D.height);
  float y_step        = line_to_pixel_length(0.2f, cmd.screen_extent2D.height);
  float x_offset_left = line_to_pixel_length(0.74f, cmd.screen_extent2D.width);
  float x_offset_right = x_offset_left + line_to_pixel_length(0.51f, cmd.screen_extent2D.width);
  int   size           = line_to_pixel_length(0.5f, cmd.screen_extent2D.height);

  // -------- left side --------
  for (int i = 0; i < 6; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);

    r.data[i].offset[0] = x_offset_left;
    r.data[i].offset[1] = y_zeroed + (y_step_modifier * y_step);
    r.data[i].value     = -5 * y_step_modifier;
    r.data[i].size      = size;
  }

  // -------- right side --------
  for (int i = 0; i < 6; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);
    int idx             = 6 + i;
    int value           = -5 * y_step_modifier;

    float additional_character_offset = ((SDL_abs(value) > 9) ? 6.0f : 0.0f) + ((value < 0) ? 6.8f : 0.0f);

    r.data[idx].offset[0] = x_offset_right - additional_character_offset;
    r.data[idx].offset[1] = y_zeroed + (y_step_modifier * y_step);
    r.data[idx].value     = value;
    r.data[idx].size      = size;
  }

  return r;
}

ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd)
{
  ArrayView<GuiHeightRulerText> r{};
  r.count = 7;
  r.data  = cmd.allocator->allocate_back<GuiHeightRulerText>(r.count);

  float start_x_offset           = line_to_pixel_length(1.18f, cmd.screen_extent2D.width);
  float start_y_offset           = line_to_pixel_length(1.58f, cmd.screen_extent2D.height);
  float y_distance_between_lines = line_to_pixel_length(0.4f, cmd.screen_extent2D.height);
  float y_pitch_modifier         = line_to_pixel_length(1.0f, cmd.screen_extent2D.height);
  int   step_between_lines       = 10;
  int   size                     = line_to_pixel_length(0.6f, cmd.screen_extent2D.height);

  for (int i = 0; i < 7; ++i)
  {
    r.data[i].offset[0] = start_x_offset;
    r.data[i].offset[1] =
        start_y_offset + ((2 - i) * y_distance_between_lines) + (y_pitch_modifier * cmd.camera_y_pitch_radians);
    r.data[i].value = SDL_abs((4 - i) * step_between_lines);
    r.data[i].size  = size;
  }

  return r;
}
