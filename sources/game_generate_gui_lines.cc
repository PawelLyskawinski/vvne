#include "game_generate_gui_lines.hh"
#include <SDL2/SDL_log.h>

namespace {

class LineSink
{
public:
  LineSink(vec2* dst, uint32_t capacity)
      : vertices(dst)
      , last_idx(0)
      , capacity(capacity)
      , counter(0)
  {
  }

  void push_offset(float x, float y, float offset_x, float offset_y) { push(x, y, x + offset_x, y + offset_y); }

  void push(float x1, float y1, float x2, float y2)
  {
    push(x1, y1);
    push(x2, y2);
    ++counter;
  }

  uint32_t reset_counter()
  {
    uint32_t r = counter;
    counter    = 0;
    return r;
  }

private:
  void push(float x, float y)
  {
    SDL_assert(last_idx < capacity);
    vec2& it = vertices[last_idx++];
    it[0]    = x;
    it[1]    = y;
  }

  vec2*    vertices;
  uint32_t last_idx;
  uint32_t capacity;
  uint32_t counter;
};

} // namespace

void generate_gui_lines(const GenerateGuiLinesCommand& cmd, vec2 dst[], uint32_t dst_capacity,
                        GuiLineSizeCount& green_counter, GuiLineSizeCount& red_counter,
                        GuiLineSizeCount& yellow_counter)
{
  //////////////////////////////////////////////////////////////////////////////
  /// Main green rulers
  //////////////////////////////////////////////////////////////////////////////

  const float width               = 0.75f;
  const float height              = 1.0f;
  const float offset_up           = 0.20f;
  const float ruler_lid_length    = 0.05f;
  const float vertical_correction = 0.008f;
  const float tiny_line_offset    = 0.011f;
  const float max_left_x          = -0.5f * width;
  const float max_right_x         = -max_left_x;
  const float top_y               = -0.5f * height - offset_up;
  const float bottom_y            = 0.5f * height - offset_up;

  green_counter  = {};
  red_counter    = {};
  yellow_counter = {};
  LineSink sink(dst, dst_capacity);

  //////////////////////////////////////////////////////////////////////////////
  // GREEN - BIG
  //////////////////////////////////////////////////////////////////////////////
  sink.push_offset(max_left_x, top_y-0.005f, ruler_lid_length, 0.0f);
  sink.push_offset(max_left_x, bottom_y+0.005f, ruler_lid_length, 0.0f);
  sink.push_offset(max_right_x, top_y-0.005f, -ruler_lid_length, 0.0f);
  sink.push_offset(max_right_x, bottom_y+0.005f, -ruler_lid_length, 0.0f);
  green_counter.big = sink.reset_counter();

  //////////////////////////////////////////////////////////////////////////////
  // GREEN - SMALL
  //////////////////////////////////////////////////////////////////////////////
  sink.push(max_left_x + ruler_lid_length, top_y - vertical_correction, max_left_x + ruler_lid_length,
            bottom_y + vertical_correction);
  sink.push(max_right_x - ruler_lid_length, top_y - vertical_correction, max_right_x - ruler_lid_length,
            bottom_y + vertical_correction);
  for (int i = 0; i < 25; ++i) /// Detail on left green ruler
  {
    const float offset         = 0.04f;
    const float big_indent     = 0.025f;
    const float small_indent   = 0.01f;
    const bool  is_longer_line = (0 == ((i + 2) % 5));

    sink.push(is_longer_line ? max_left_x + small_indent : max_left_x + big_indent, top_y + (i * offset),
              max_left_x + ruler_lid_length - tiny_line_offset, top_y + (i * offset));
  }
  green_counter.small = sink.reset_counter();

  //////////////////////////////////////////////////////////////////////////////
  // GREEN - TINY
  //////////////////////////////////////////////////////////////////////////////
  sink.push(max_left_x + ruler_lid_length - tiny_line_offset, top_y - vertical_correction,
            max_left_x + ruler_lid_length - tiny_line_offset, bottom_y + vertical_correction);
  sink.push(max_right_x - ruler_lid_length + tiny_line_offset, top_y - vertical_correction,
            max_right_x - ruler_lid_length + tiny_line_offset, bottom_y + vertical_correction);

  {
    // Green speed meter frame
    const float length  = 0.125f;
    const float upper_y = -0.202f;

    // 3 main horizontal lines
    sink.push_offset(max_left_x - 0.09f - (0.5f * length), upper_y + 0.000f, length, 0.0f);
    sink.push_offset(max_left_x - 0.09f - (0.5f * length), upper_y + 0.040f, length, 0.0f);
    sink.push_offset(max_left_x - 0.09f - (0.5f * length), upper_y + 0.065f, length, 0.0f);

    // 2 main side vertical lines
    sink.push_offset(max_left_x - 0.09f - (0.5f * length), upper_y, 0.0f, 0.065f);
    sink.push_offset(max_left_x - 0.09f + (0.5f * length), upper_y, 0.0f, 0.065f);

    // "SPEED" text inside speed meter frame
    float letter_left_x        = max_left_x - 0.0f - length;
    float letter_bottom_y      = upper_y + 0.0595f;
    float letter_width         = 0.01f;
    float letter_height        = 0.014f;
    float letter_space_between = 0.005f;

    // S letter
    sink.push_offset(letter_left_x, letter_bottom_y, letter_width, 0.0f);
    sink.push_offset(letter_left_x, letter_bottom_y - (0.5f * letter_height), letter_width, 0.0f);
    sink.push_offset(letter_left_x, letter_bottom_y - letter_height, letter_width, 0.0f);
    sink.push_offset(letter_left_x + letter_width, letter_bottom_y, 0.0f, -(0.5f * letter_height));
    sink.push_offset(letter_left_x, letter_bottom_y - (0.5f * letter_height), 0.0f, -(0.5f * letter_height));

    // P letter
    letter_left_x += letter_width + letter_space_between;
    sink.push_offset(letter_left_x, letter_bottom_y, 0.0f, -letter_height);
    sink.push_offset(letter_left_x, letter_bottom_y - letter_height, letter_width, 0.0f);
    sink.push_offset(letter_left_x + letter_width, letter_bottom_y - letter_height, 0.0f, 0.5f * letter_height);
    sink.push_offset(letter_left_x + letter_width, letter_bottom_y - (0.5f * letter_height), -letter_width, 0.0f);

    // E letters
    for (int i = 0; i < 2; ++i)
    {
      letter_left_x += letter_width + letter_space_between;
      sink.push_offset(letter_left_x, letter_bottom_y, 0.0f, -letter_height);
      sink.push_offset(letter_left_x, letter_bottom_y - letter_height, letter_width, 0.0f);
      sink.push_offset(letter_left_x, letter_bottom_y - (0.5f * letter_height), letter_width, 0.0f);
      sink.push_offset(letter_left_x, letter_bottom_y, letter_width, 0.0f);
    }

    // D letter
    letter_left_x += letter_width + letter_space_between;
    sink.push_offset(letter_left_x, letter_bottom_y, 0.0f, -letter_height);
    sink.push_offset(letter_left_x, letter_bottom_y - letter_height, 0.75f * letter_width, 0.0f);
    sink.push_offset(letter_left_x, letter_bottom_y, 0.75f * letter_width, 0.0f);
    sink.push_offset(letter_left_x + (0.75f * letter_width), letter_bottom_y - letter_height, 0.25f * letter_width,
                     0.25f * letter_height);
    sink.push_offset(letter_left_x + (0.75f * letter_width), letter_bottom_y, 0.25f * letter_width,
                     -(0.25f * letter_height));
    sink.push_offset(letter_left_x + letter_width, letter_bottom_y - (0.25f * letter_height), 0.0f,
                     -(0.5f * letter_height));

    // "km/h" text inside speed meter frame
    letter_left_x              = max_left_x + 0.04f - length;
    letter_bottom_y            = upper_y + 0.033f;
    letter_width               = 0.01f;
    letter_height              = 0.025f;
    letter_space_between       = 0.003f;
    const float letter_y_guide = letter_bottom_y - (0.6f * letter_height);

    // K letter
    sink.push_offset(letter_left_x, letter_bottom_y, 0.0f, -letter_height);
    sink.push_offset(letter_left_x, letter_bottom_y - (0.2f * letter_height), letter_width, -(0.4f * letter_height));
    sink.push(letter_left_x + (0.5f * letter_width), letter_bottom_y - (0.35f * letter_height),
              letter_left_x + letter_width, letter_bottom_y);

    // M letter
    letter_left_x += letter_width + letter_space_between;
    sink.push_offset(letter_left_x, letter_y_guide, letter_width, 0.0f);
    sink.push(letter_left_x, letter_bottom_y, letter_left_x, letter_y_guide);
    sink.push(letter_left_x + (0.5f * letter_width), letter_bottom_y, letter_left_x + (0.5f * letter_width),
              letter_y_guide);
    sink.push(letter_left_x + letter_width, letter_bottom_y, letter_left_x + letter_width, letter_y_guide);

    // slash
    letter_left_x += letter_width + letter_space_between;
    sink.push(letter_left_x, letter_bottom_y, letter_left_x + letter_width, letter_bottom_y - letter_height);

    // H letter
    letter_left_x += letter_width + letter_space_between;
    sink.push(letter_left_x, letter_bottom_y, letter_left_x, letter_bottom_y - letter_height);
    sink.push(letter_left_x, letter_y_guide, letter_left_x + letter_width, letter_y_guide);
    sink.push(letter_left_x + letter_width, letter_bottom_y, letter_left_x + letter_width, letter_y_guide);
  }

  // Compass border
  {
    float width           = 0.5f;
    float height          = 0.04f;
    float bottom_y_offset = 0.38f;

    sink.push_offset(-0.5f * width, bottom_y_offset, width, 0.0f);
    sink.push_offset(-0.5f * width, bottom_y_offset - height, width, 0.0f);
    sink.push_offset(-0.5f * width, bottom_y_offset, 0.0f, -height);
    sink.push_offset(0.5f * width, bottom_y_offset, 0.0f, -height);
  }

  green_counter.tiny = sink.reset_counter();

  //////////////////////////////////////////////////////////////////////////////
  /// RED - TINY
  //////////////////////////////////////////////////////////////////////////////
  const float red_x_offset                 = 0.02f;
  const float height_ruler_length          = 0.04f;
  const float height_ruler_left_x_position = max_left_x + ruler_lid_length + red_x_offset;

  for (int side = 0; side < 2; ++side)
  {
    for (int i = 0; i < 5; ++i)
    {
      const float side_mod = (0 < side) ? -1.0f : 1.0f;

      vec2 base_offset = {side_mod * height_ruler_left_x_position, cmd.player_y_location_meters / 8.0f};

      // endless repetition
      while (base_offset[1] > -0.5f)
        base_offset[1] -= 0.8f;

      const vec2 size = {side_mod * height_ruler_length, 0.2f};

      vec2 offset     = {};
      vec2 correction = {0.0f, i * 0.4f};
      vec2_add(offset, correction, base_offset);

      sink.push_offset(offset[0], offset[1] + (0.5f * size[1]), size[0], 0.0f);
      sink.push_offset(offset[0], offset[1] + (0.5f * size[1]), 0.0f, -size[1]);
      sink.push_offset(offset[0], offset[1] - (0.5f * size[1]), size[0], 0.0f);
    }
  }
  red_counter.tiny = sink.reset_counter();

  //////////////////////////////////////////////////////////////////////////////
  /// YELLOW - SMALL
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

    sink.push(
        x_left * rotation_matrix[0] + y * rotation_matrix[2], x_left * rotation_matrix[1] + y * rotation_matrix[3],
        x_right * rotation_matrix[0] + y * rotation_matrix[2], x_right * rotation_matrix[1] + y * rotation_matrix[3]);
  }
  yellow_counter.small = sink.reset_counter();
}

static uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

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

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  float y_zeroed = line_to_pixel_length(-(cmd.player_y_location_meters / 8.0f - 1.23f), cmd.screen_extent2D.height);
  float y_step   = line_to_pixel_length(0.2f, cmd.screen_extent2D.height);
  float x_offset_left  = line_to_pixel_length(0.74f, cmd.screen_extent2D.width);
  float x_offset_right = x_offset_left + line_to_pixel_length(0.51f, cmd.screen_extent2D.width);
  int   size           = line_to_pixel_length(0.5f, cmd.screen_extent2D.height);

  StackAdapter<GuiHeightRulerText> stack(allocator);

  // -------- left side --------
  for (int i = 0; i < 7; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);
    y_step_modifier += static_cast<int>(cmd.player_y_location_meters * 0.6f) - 2;

    GuiHeightRulerText item = {{x_offset_left, y_zeroed + (y_step_modifier * y_step)}, size, -5 * y_step_modifier};
    stack.push(&item, 1);
  }

  // -------- right side --------
  for (int i = 0; i < 7; ++i)
  {
    int y_step_modifier = (i < 4) ? (-1 * i) : (i - 3);
    y_step_modifier += static_cast<int>(cmd.player_y_location_meters * 0.6f) - 2;

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
