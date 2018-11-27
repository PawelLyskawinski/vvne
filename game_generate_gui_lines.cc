#include "game.hh"

template <typename T> class StackAdapter
{
public:
  explicit StackAdapter(Stack& main_allocator)
      : main_allocator(main_allocator)
      , stack(reinterpret_cast<T*>(&main_allocator.data[main_allocator.sp]))
      , size(0)
  {
  }

  void push(const T items[], const uint32_t n)
  {
    SDL_memcpy(main_allocator.alloc<T>(n), items, n * sizeof(T));
    size += n;
  }

  ArrayView<T> to_arrayview() const { return {stack, size}; }

private:
  Stack& main_allocator;
  T*     stack;
  int    size;
};

ArrayView<GuiLine> generate_gui_lines(const GenerateGuiLinesCommand& cmd, Stack& allocator)
{
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

  StackAdapter<GuiLine> stack(allocator);

  {
    const GuiLine lines[] = {
        // clang-format off
        // A                                                                                 B                                                                                   SIZE                  COLOR
        { {max_left_x, top_y},                                                              {max_left_x + ruler_lid_length, top_y},                                              GuiLine::Size::Big,   GuiLine::Color::Green },
        { {max_left_x + ruler_lid_length, top_y - vertical_correction},                     {max_left_x + ruler_lid_length, bottom_y + vertical_correction},                     GuiLine::Size::Small, GuiLine::Color::Green },
        { {max_left_x + ruler_lid_length - tiny_line_offset, top_y - vertical_correction},  {max_left_x + ruler_lid_length - tiny_line_offset, bottom_y + vertical_correction},  GuiLine::Size::Tiny,  GuiLine::Color::Green },
        { {max_left_x, bottom_y},                                                           {max_left_x + ruler_lid_length, bottom_y},                                           GuiLine::Size::Big,   GuiLine::Color::Green },
        { {max_right_x - ruler_lid_length, top_y},                                          {max_right_x, top_y},                                                                GuiLine::Size::Big,   GuiLine::Color::Green },
        { {max_right_x - ruler_lid_length, top_y - vertical_correction},                    {max_right_x - ruler_lid_length, bottom_y + vertical_correction},                    GuiLine::Size::Small, GuiLine::Color::Green },
        { {max_right_x - ruler_lid_length + tiny_line_offset, top_y - vertical_correction}, {max_right_x - ruler_lid_length + tiny_line_offset, bottom_y + vertical_correction}, GuiLine::Size::Tiny,  GuiLine::Color::Green },
        { {max_right_x, bottom_y},                                                          {max_right_x - ruler_lid_length, bottom_y},                                          GuiLine::Size::Big,   GuiLine::Color::Green },
        // clang-format on
    };
    stack.push(lines, SDL_arraysize(lines));
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Detail on left green ruler
  //////////////////////////////////////////////////////////////////////////////

  for (int i = 0; i < 25; ++i)
  {
    const float offset       = 0.04f;
    const float big_indent   = 0.025f;
    const float small_indent = 0.01f;

    GuiLine line = {
        {max_left_x + big_indent, top_y + (i * offset)},
        {max_left_x + ruler_lid_length - tiny_line_offset, top_y + (i * offset)},
        GuiLine::Size::Small,
        GuiLine::Color::Green,
    };

    if (0 == ((i + 2) % 5))
      line.a[0] = max_left_x + small_indent;

    stack.push(&line, 1);
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
      const float side_mod   = (0 < side) ? -1.0f : 1.0f;
      const vec2 base_offset = {side_mod * height_ruler_left_x_position, -1.2f - (cmd.player_y_location_meters / 8.0f)};
      const vec2 size        = {side_mod * height_ruler_length, 0.2f};

      vec2 offset     = {};
      vec2 correction = {0.0f, i * 0.4f};
      vec2_add(offset, correction, base_offset);

      GuiLine lines[] = {
          // clang-format off
          { {offset[0], offset[1] + size[1] / 2.0f}, {offset[0] + size[0], offset[1] + size[1] / 2.0f}, GuiLine::Size::Tiny, GuiLine::Color::Red },
          { {offset[0], offset[1] + size[1] / 2.0f}, {offset[0], offset[1] - size[1] / 2.0f}, GuiLine::Size::Tiny, GuiLine::Color::Red },
          { {offset[0], offset[1] - size[1] / 2.0f}, {offset[0] + size[0], offset[1] - size[1] / 2.0f}, GuiLine::Size::Tiny, GuiLine::Color::Red }
          // clang-format on
      };

      stack.push(lines, SDL_arraysize(lines));
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Yellow pitch rulers
  //////////////////////////////////////////////////////////////////////////////

  for (int i = 0; i < 7; ++i)
  {
    const float distance_from_main = 0.16f;
    const float horizontal_offset  = 0.4f;

    const float x_left  = (max_left_x + ruler_lid_length + distance_from_main);
    const float x_right = -x_left;
    const float y       = -offset_up + (i * horizontal_offset) - (2 * horizontal_offset) + cmd.camera_y_pitch_radians;

    float rotation_matrix[] = {SDL_cosf(cmd.camera_x_pitch_radians), -1.0f * SDL_sinf(cmd.camera_x_pitch_radians),
                               SDL_sinf(cmd.camera_x_pitch_radians), SDL_cosf(cmd.camera_x_pitch_radians)};

    GuiLine line = {
        {x_left * rotation_matrix[0] + y * rotation_matrix[2], x_left * rotation_matrix[1] + y * rotation_matrix[3]},
        {x_right * rotation_matrix[0] + y * rotation_matrix[2], x_right * rotation_matrix[1] + y * rotation_matrix[3]},
        GuiLine::Size::Small,
        GuiLine::Color::Yellow,
    };

    stack.push(&line, 1);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Green speed meter frame
  //////////////////////////////////////////////////////////////////////////////
  {
    const float length  = 0.125f;
    const float upper_y = -0.202f;

    {
      const GuiLine lines[] = {
          // clang-format off
          // A                                                         B                                                        SIZE                 COLOR
          // 3 main horizontal lines
          { {max_left_x - 0.09f - (length / 2.0f), upper_y},          {max_left_x - 0.09f + (length / 2.0f), upper_y},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {max_left_x - 0.09f - (length / 2.0f), upper_y + 0.04f},  {max_left_x - 0.09f + (length / 2.0f), upper_y + 0.04f},  GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {max_left_x - 0.09f - (length / 2.0f), upper_y + 0.065f}, {max_left_x - 0.09f + (length / 2.0f), upper_y + 0.065f}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          // 2 main side vertical lines
          { {max_left_x - 0.09f - (length / 2.0f), upper_y},          {max_left_x - 0.09f - (length / 2.0f), upper_y + 0.065f}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {max_left_x - 0.09f + (length / 2.0f), upper_y},          {max_left_x - 0.09f + (length / 2.0f), upper_y + 0.065f}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };

      stack.push(lines, SDL_arraysize(lines));
    }

    // "SPEED" text inside speed meter frame
    // 'S' - 5 lines
    // 'P' - 4 lines
    // 'E' - 4 lines
    // 'E' - 4 lines
    // 'D' - 6 lines
    // -------------
    //      23 lines

    float letter_left_x        = max_left_x - 0.0f - length;
    float letter_bottom_y      = upper_y + 0.0595f;
    float letter_width         = 0.01f;
    float letter_height        = 0.014f;
    float letter_space_between = 0.005f;

    {
      // S letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                          B                                                                        SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                          {letter_left_x + letter_width, letter_bottom_y},                          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - (letter_height / 2.0f)}, {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - letter_height},          {letter_left_x + letter_width, letter_bottom_y - letter_height},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y},           {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - (letter_height / 2.0f)}, {letter_left_x, letter_bottom_y - letter_height},                         GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // P letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                                         B                                                                        SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                                         {letter_left_x, letter_bottom_y - letter_height},                         GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - letter_height},                         {letter_left_x + letter_width, letter_bottom_y - letter_height},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y - letter_height},          {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, {letter_left_x, letter_bottom_y - (letter_height / 2.0f)},                GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // E (1st) letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                          B                                                                        SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                          {letter_left_x,                letter_bottom_y - letter_height},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - letter_height},          {letter_left_x + letter_width, letter_bottom_y - letter_height},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - (letter_height / 2.0f)}, {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y},                          {letter_left_x + letter_width, letter_bottom_y},                          GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // E (2nd) letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                          B                                                                        SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                          {letter_left_x, letter_bottom_y - letter_height},                         GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - letter_height},          {letter_left_x + letter_width, letter_bottom_y - letter_height},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - (letter_height / 2.0f)}, {letter_left_x + letter_width, letter_bottom_y - (letter_height / 2.0f)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y},                          {letter_left_x + letter_width, letter_bottom_y},                          GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // D letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                                          B                                                                         SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                                          {letter_left_x, letter_bottom_y - letter_height},                          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - letter_height},                          {letter_left_x + (0.75f * letter_width), letter_bottom_y - letter_height}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y},                                          {letter_left_x + (0.75f * letter_width), letter_bottom_y},                 GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + (0.75f * letter_width), letter_bottom_y - letter_height}, {letter_left_x + letter_width, letter_bottom_y - (0.75f * letter_height)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + (0.75f * letter_width), letter_bottom_y},                 {letter_left_x + letter_width, letter_bottom_y - (0.25f * letter_height)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y - (0.25f * letter_height)}, {letter_left_x + letter_width, letter_bottom_y - (0.75f * letter_height)}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    // "km/h" text inside speed meter frame
    // 'k' - 3 lines
    // 'm' - 4 lines
    // '/' - 1 line
    // 'h' - 3 lines
    // -------------
    //      11 lines

    letter_left_x              = max_left_x + 0.04f - length;
    letter_bottom_y            = upper_y + 0.033f;
    letter_width               = 0.01f;
    letter_height              = 0.025f;
    letter_space_between       = 0.003f;
    const float letter_y_guide = letter_bottom_y - (0.6f * letter_height);

    {
      // K letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                                                   B                                                SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                                                   {letter_left_x, letter_bottom_y - letter_height}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y - (0.2f * letter_height)},                          {letter_left_x + letter_width, letter_y_guide},   GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + (0.5f * letter_width), letter_bottom_y - (0.35f * letter_height)}, {letter_left_x + letter_width, letter_bottom_y},  GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // M letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                         B                                                       SIZE                 COLOR
          { {letter_left_x, letter_y_guide},                          {letter_left_x + letter_width, letter_y_guide},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_bottom_y},                         {letter_left_x, letter_y_guide},                         GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + (0.5f * letter_width), letter_bottom_y}, {letter_left_x + (0.5f * letter_width), letter_y_guide}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y},          {letter_left_x + letter_width, letter_y_guide},          GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // clang-format off
      //                 A                                 B                                                               SIZE                 COLOR
      GuiLine slash = { {letter_left_x, letter_bottom_y}, {letter_left_x + letter_width, letter_bottom_y - letter_height}, GuiLine::Size::Tiny, GuiLine::Color::Green };
      // clang-format on
      stack.push(&slash, 1);
    }

    letter_left_x += letter_width + letter_space_between;

    {
      // H letter
      const GuiLine lines[] = {
          // clang-format off
          // A                                                B                                                SIZE                 COLOR
          { {letter_left_x, letter_bottom_y},                {letter_left_x, letter_bottom_y - letter_height}, GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x, letter_y_guide},                 {letter_left_x + letter_width, letter_y_guide},   GuiLine::Size::Tiny, GuiLine::Color::Green },
          { {letter_left_x + letter_width, letter_bottom_y}, {letter_left_x + letter_width, letter_y_guide},   GuiLine::Size::Tiny, GuiLine::Color::Green },
          // clang-format on
      };
      stack.push(lines, SDL_arraysize(lines));
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Compass border
  //////////////////////////////////////////////////////////////////////////////
  {
    float width           = 0.5f;
    float height          = 0.04f;
    float bottom_y_offset = 0.38f;

    const GuiLine lines[] = {
        // clang-format off
        // A                                          B                                         SIZE                 COLOR
        { {-0.5f * width, bottom_y_offset},          {0.5f * width, bottom_y_offset},           GuiLine::Size::Tiny, GuiLine::Color::Green },
        { {-0.5f * width, bottom_y_offset - height}, {0.5f * width, bottom_y_offset - height},  GuiLine::Size::Tiny, GuiLine::Color::Green },
        { {-0.5f * width, bottom_y_offset},          {-0.5f * width, bottom_y_offset - height}, GuiLine::Size::Tiny, GuiLine::Color::Green },
        { {0.5f * width, bottom_y_offset},           {0.5f * width, bottom_y_offset - height},  GuiLine::Size::Tiny, GuiLine::Color::Green },
        // clang-format on
    };
    stack.push(lines, SDL_arraysize(lines));
  }

  return stack.to_arrayview();
}

static uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  float y_zeroed      = line_to_pixel_length(0.88f - (cmd.player_y_location_meters / 8.0f), cmd.screen_extent2D.height);
  float y_step        = line_to_pixel_length(0.2f, cmd.screen_extent2D.height);
  float x_offset_left = line_to_pixel_length(0.74f, cmd.screen_extent2D.width);
  float x_offset_right = x_offset_left + line_to_pixel_length(0.51f, cmd.screen_extent2D.width);
  int   size           = line_to_pixel_length(0.5f, cmd.screen_extent2D.height);

  StackAdapter<GuiHeightRulerText> stack(allocator);

  // -------- left side --------
  for (int i = 0; i < 6; ++i)
  {
    const int          y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);
    GuiHeightRulerText item = {{x_offset_left, y_zeroed + (y_step_modifier * y_step)}, size, -5 * y_step_modifier};
    stack.push(&item, 1);
  }

  // -------- right side --------
  for (int i = 0; i < 6; ++i)
  {
    const int   y_step_modifier             = (i < 4) ? (-1 * i) : (i - 3);
    const int   value                       = -5 * y_step_modifier;
    const float additional_character_offset = ((SDL_abs(value) > 9) ? 6.0f : 0.0f) + ((value < 0) ? 6.8f : 0.0f);

    GuiHeightRulerText item = {
        {x_offset_right - additional_character_offset, y_zeroed + (y_step_modifier * y_step)}, size, value};
    stack.push(&item, 1);
  }

  return stack.to_arrayview();
}

ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  float start_x_offset           = line_to_pixel_length(1.18f, cmd.screen_extent2D.width);
  float start_y_offset           = line_to_pixel_length(1.58f, cmd.screen_extent2D.height);
  float y_distance_between_lines = line_to_pixel_length(0.4f, cmd.screen_extent2D.height);
  float y_pitch_modifier         = line_to_pixel_length(1.0f, cmd.screen_extent2D.height);
  int   step_between_lines       = 10;
  int   size                     = line_to_pixel_length(0.6f, cmd.screen_extent2D.height);

  StackAdapter<GuiHeightRulerText> stack(allocator);

  for (int i = 0; i < 7; ++i)
  {
    GuiHeightRulerText item = {{start_x_offset, start_y_offset + ((2 - i) * y_distance_between_lines) +
                                                    (y_pitch_modifier * cmd.camera_y_pitch_radians)},
                               size,
                               SDL_abs((4 - i) * step_between_lines)};
    stack.push(&item, 1);
  }

  return stack.to_arrayview();
}
