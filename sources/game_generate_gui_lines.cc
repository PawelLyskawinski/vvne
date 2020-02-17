#include "game_generate_gui_lines.hh"
#include <SDL2/SDL_log.h>

static uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

template <typename T> class StackAdapter
{
public:
  explicit StackAdapter(Stack& main_allocator)
      : main_allocator(main_allocator)
      , stack(nullptr)
      , size(0)
  {
  }

  void push(const T items[], const uint32_t n)
  {
    T* allocation = main_allocator.alloc<T>(n);
    if (nullptr == stack)
    {
      stack = allocation;
    }
    SDL_memcpy(allocation, items, n * sizeof(T));
    size += n;
  }

  [[nodiscard]] ArrayView<T> to_arrayview() const
  {
    return {stack, size};
  }

private:
  Stack& main_allocator;
  T*     stack;
  int    size;
};

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  StackAdapter<GuiHeightRulerText> stack(allocator);
  const Vec2                       base_offset = Vec2(0.13f, cmd.player_y_location_meters / 16.0f - 1.015f);

  for (uint32_t side = 0; side < 2; ++side)
  {
    for (int i = 0; i < 6; ++i)
    {
      Vec2 offset = base_offset;
      if (side)
      {
        offset.x *= -1.0f;
      }
      else
      {
        offset.x -= 0.016f;
      }

      offset += Vec2(0.5f, 0.5f);
      offset.y *= -1.0f;
      offset.y += (i * 0.1f);

      int height_number = 15 - (5 * i);

      if (offset.y < 0.12f)
      {
        offset.y += 0.6f;
        height_number -= 30;
      }

      offset = offset.scale(Vec2(cmd.screen_extent2D.width, cmd.screen_extent2D.height));

      GuiHeightRulerText item = {offset, (int)line_to_pixel_length(0.5f, cmd.screen_extent2D.height), height_number};
      stack.push(&item, 1);
    }
  }

  return stack.to_arrayview();
}

ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  float start_x_offset           = line_to_pixel_length(1.18f, cmd.screen_extent2D.width);
  float start_y_offset           = line_to_pixel_length(1.58f, cmd.screen_extent2D.height);
  float y_distance_between_lines = line_to_pixel_length(0.4f, cmd.screen_extent2D.height);
  float y_pitch_modifier         = line_to_pixel_length(1.0f, cmd.screen_extent2D.height);
  int   step_between_lines       = 20;
  int   size                     = line_to_pixel_length(0.6f, cmd.screen_extent2D.height);

  StackAdapter<GuiHeightRulerText> stack(allocator);

  for (int i = 0; i < 7; ++i)
  {
    GuiHeightRulerText item = {{start_x_offset, start_y_offset + ((2 - i) * y_distance_between_lines) +
                                                    (y_pitch_modifier * cmd.camera_y_pitch_radians)},
                               size,
                               -(4 - i) * step_between_lines + 10};
    stack.push(&item, 1);
  }

  return stack.to_arrayview();
}
