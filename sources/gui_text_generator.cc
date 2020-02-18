#include "gui_text_generator.hh"
#include <SDL2/SDL_log.h>

namespace {

uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

} // namespace

ArrayView<GuiText> GuiTextGenerator::height_ruler(Stack& allocator) const
{
  ArrayView<GuiText> stack = {};
  stack.data               = allocator.alloc<GuiText>(12);

  const Vec2 base_offset = Vec2(0.13f, player_y_location_meters / 16.0f - 1.015f);
  const Vec3 green       = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);
  const Vec3 red         = Vec3(1.0f, 0.0f, 0.0f);

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

      offset = offset.scale(Vec2(screen_extent2D.width, screen_extent2D.height));

      const GuiText item = {offset, 0 <= height_number ? green : red,
                            static_cast<int>(line_to_pixel_length(0.5f, screen_extent2D.height)), height_number};

      stack[stack.count++] = item;
    }
  }

  return stack;
}

ArrayView<GuiText> GuiTextGenerator::tilt_ruler(Stack& allocator) const
{
  float start_x_offset           = line_to_pixel_length(1.17f, screen_extent2D.width);
  float start_y_offset           = line_to_pixel_length(1.375f, screen_extent2D.height);
  float y_distance_between_lines = line_to_pixel_length(0.4f, screen_extent2D.height);
  float y_pitch_modifier         = line_to_pixel_length(1.0f, screen_extent2D.height);
  int   step_between_lines       = 20;
  int   size                     = line_to_pixel_length(0.6f, screen_extent2D.height);

  const Vec3 green  = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);
  const Vec3 yellow = Vec3(1.0f, 1.0f, 0.0f);

  ArrayView<GuiText> stack = {};
  stack.data               = allocator.alloc<GuiText>(12);

  for (int i = 0; i < 10; ++i)
  {
    const GuiText item = {{start_x_offset, start_y_offset + ((3 - i) * y_distance_between_lines) +
                                               (y_pitch_modifier * camera_y_pitch_radians)},
                          3 < i ? green : yellow,
                          size,
                          -(4 - i) * step_between_lines};

    stack[stack.count++] = item;
  }

  return stack;
}
