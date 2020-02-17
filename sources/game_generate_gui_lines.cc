#include "game_generate_gui_lines.hh"
#include <SDL2/SDL_log.h>

static uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  ArrayView<GuiHeightRulerText> stack = {};
  stack.data                          = allocator.alloc<GuiHeightRulerText>(12);

  const Vec2 base_offset = Vec2(0.13f, cmd.player_y_location_meters / 16.0f - 1.015f);
  const Vec3 color       = Vec3(1.0f, 0.0f, 0.0f);

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

      GuiHeightRulerText item = {offset, color, (int)line_to_pixel_length(0.5f, cmd.screen_extent2D.height),
                                 height_number};
      stack[stack.count++]    = item;
    }
  }

  return stack;
}

ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator)
{
  float      start_x_offset           = line_to_pixel_length(1.18f, cmd.screen_extent2D.width);
  float      start_y_offset           = line_to_pixel_length(1.58f, cmd.screen_extent2D.height);
  float      y_distance_between_lines = line_to_pixel_length(0.4f, cmd.screen_extent2D.height);
  float      y_pitch_modifier         = line_to_pixel_length(1.0f, cmd.screen_extent2D.height);
  int        step_between_lines       = 20;
  int        size                     = line_to_pixel_length(0.6f, cmd.screen_extent2D.height);
  const Vec3 color                    = Vec3(1.0f, 1.0f, 0.0f);

  ArrayView<GuiHeightRulerText> stack = {};
  stack.data                          = allocator.alloc<GuiHeightRulerText>(12);

  for (int i = 0; i < 7; ++i)
  {
    GuiHeightRulerText item = {{start_x_offset, start_y_offset + ((2 - i) * y_distance_between_lines) +
                                                    (y_pitch_modifier * cmd.camera_y_pitch_radians)},
                               color,
                               size,
                               -(4 - i) * step_between_lines + 10};
    stack[stack.count++]    = item;
  }

  return stack;
}
