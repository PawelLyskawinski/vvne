#include "gui_lines_renderer.hh"
#include "lines_renderer.hh"

constexpr float width               = 0.75f;
constexpr float height              = 1.0f;
constexpr float offset_up           = 0.20f;
constexpr float ruler_lid_length    = 0.05f;
constexpr float vertical_correction = 0.008f;
constexpr float tiny_line_offset    = 0.011f;
constexpr float max_left_x          = -0.5f * width;
constexpr float max_right_x         = -max_left_x;
constexpr float top_y               = -0.5f * height - offset_up;
constexpr float bottom_y            = 0.5f * height - offset_up;

namespace size {

constexpr float Huge   = 7.0f;
constexpr float Normal = 5.0f;
constexpr float Small  = 3.0f;
constexpr float Tiny   = 1.0f;

} // namespace size

void render_constant_lines(LinesRenderer& renderer)
{
  const Vec4 green_color = Vec4(Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f), 0.9f);

  Line l;
  l.color = green_color;

  //   0 --> 1
  //         |
  //         |
  //         |
  //         |
  //   3 <-- 2
  //

  l.width     = size::Huge;
  l.origin    = Vec2(max_left_x, top_y - vertical_correction);
  l.direction = Vec2(ruler_lid_length, 0.0f);
  renderer.push(l);

  l.width     = size::Small;
  l.origin    = Vec2(max_left_x + ruler_lid_length - 0.002f, top_y - vertical_correction);
  l.direction = Vec2(0.0f, bottom_y + vertical_correction - top_y);
  renderer.push(l);

  l.width     = size::Huge;
  l.origin    = Vec2(max_left_x, bottom_y + 0.005f);
  l.direction = Vec2(ruler_lid_length, 0.0f);
  renderer.push(l);

  //   1 <-- 0
  //   |
  //   |
  //   |
  //   |
  //   2 --> 3
  //

  l.width     = size::Huge;
  l.origin    = Vec2(max_right_x, top_y - 0.005f);
  l.direction = Vec2(-ruler_lid_length, 0.0f);
  renderer.push(l);

  l.width     = size::Small;
  l.origin    = Vec2(max_right_x - ruler_lid_length + 0.002f, top_y - vertical_correction);
  l.direction = Vec2(0.0f, bottom_y + vertical_correction - top_y);
  renderer.push(l);

  l.width     = size::Huge;
  l.origin    = Vec2(max_right_x, bottom_y + 0.005f);
  l.direction = Vec2(-ruler_lid_length, 0.0f);
  renderer.push(l);

  //
  // Small accent
  //

  l.width     = size::Tiny;
  l.origin    = Vec2(max_right_x - ruler_lid_length + tiny_line_offset, top_y);
  l.direction = Vec2(0.0f, 1.0);
  renderer.push(l);

  //
  // Green speed meter frame
  //

  {
    const float length  = 0.125f;
    const float upper_y = -0.202f;

    //
    // 3 main horizontal lines
    //

    l.direction             = Vec2(length, 0.0f);
    const float y_offsets[] = {0.000f, 0.040f, 0.065f};
    for (float y_offset : y_offsets)
    {
      l.origin = Vec2(max_left_x - 0.09f - (0.5f * length), upper_y + y_offset);
      renderer.push(l);
    }

    //
    // 2 main side vertical lines
    //

    l.direction = Vec2(0.0f, 0.065f);

    l.origin = Vec2(max_left_x - 0.09f - (0.5f * length), upper_y);
    renderer.push(l);

    l.origin = Vec2(max_left_x - 0.09f + (0.5f * length), upper_y);
    renderer.push(l);

    //
    // "SPEED" text inside speed meter frame
    //

    float letter_left_x        = max_left_x - 0.0f - length;
    float letter_bottom_y      = upper_y + 0.0595f;
    float letter_width         = 0.01f;
    float letter_height        = 0.014f;
    float letter_space_between = 0.005f;

    //
    // S letter
    //

    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - (0.5f * letter_height));
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - letter_height);
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y);
    l.direction = Vec2(0.0f, -(0.5f * letter_height));
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - (0.5f * letter_height));
    l.direction = Vec2(0.0f, -(0.5f * letter_height));
    renderer.push(l);

    //
    // P letter
    //

    letter_left_x += letter_width + letter_space_between;

    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.0f, -letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - letter_height);
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y - letter_height);
    l.direction = Vec2(0.0f, 0.5f * letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y - (0.5f * letter_height));
    l.direction = Vec2(-letter_width, 0.0f);
    renderer.push(l);

    //
    // E letters
    //

    for (int i = 0; i < 2; ++i)
    {
      letter_left_x += letter_width + letter_space_between;
      l.origin    = Vec2(letter_left_x, letter_bottom_y);
      l.direction = Vec2(0.0f, -letter_height);
      renderer.push(l);

      l.origin    = Vec2(letter_left_x, letter_bottom_y - letter_height);
      l.direction = Vec2(letter_width, 0.0f);
      renderer.push(l);

      l.origin    = Vec2(letter_left_x, letter_bottom_y - (0.5f * letter_height));
      l.direction = Vec2(letter_width, 0.0f);
      renderer.push(l);

      l.origin    = Vec2(letter_left_x, letter_bottom_y);
      l.direction = Vec2(letter_width, 0.0f);
      renderer.push(l);
    }

    //
    // D letter
    //

    letter_left_x += letter_width + letter_space_between;
    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.0f, -letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - letter_height);
    l.direction = Vec2(0.75f * letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.75f * letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + (0.75f * letter_width), letter_bottom_y - letter_height);
    l.direction = Vec2(0.25f * letter_width, 0.25f * letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + (0.75f * letter_width), letter_bottom_y);
    l.direction = Vec2(0.25f * letter_width, -(0.25f * letter_height));
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y - (0.25f * letter_height));
    l.direction = Vec2(0.0f, -(0.5f * letter_height));
    renderer.push(l);

    //
    // "km/h" text inside speed meter frame
    //

    letter_left_x              = max_left_x + 0.04f - length;
    letter_bottom_y            = upper_y + 0.033f;
    letter_width               = 0.01f;
    letter_height              = 0.025f;
    letter_space_between       = 0.003f;
    const float letter_y_guide = -(0.6f * letter_height);

    //
    // K letter
    //

    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.0f, -letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y - (0.2f * letter_height));
    l.direction = Vec2(letter_width, -(0.4f * letter_height));
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + (0.5f * letter_width), letter_bottom_y - (0.35f * letter_height));
    l.direction = Vec2(0.5f * letter_width, 0.008f);
    renderer.push(l);

    //
    // M letter
    //

    letter_left_x += letter_width + letter_space_between;
    l.origin    = Vec2(letter_left_x, letter_bottom_y + letter_y_guide);
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.0f, letter_y_guide);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + (0.5f * letter_width), letter_bottom_y);
    l.direction = Vec2(0.0f, letter_y_guide);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y);
    l.direction = Vec2(0.0f, letter_y_guide);
    renderer.push(l);

    //
    // slash
    //

    letter_left_x += letter_width + letter_space_between;
    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(letter_width, -letter_height);
    renderer.push(l);

    //
    // H letter
    //

    letter_left_x += letter_width + letter_space_between;
    l.origin    = Vec2(letter_left_x, letter_bottom_y);
    l.direction = Vec2(0.0f, -letter_height);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x, letter_bottom_y + letter_y_guide);
    l.direction = Vec2(letter_width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(letter_left_x + letter_width, letter_bottom_y);
    l.direction = Vec2(0.0f, letter_y_guide);
    renderer.push(l);
  }

  //
  // Compass border
  //

  {
    float width           = 0.5f;
    float height          = 0.04f;
    float bottom_y_offset = 0.38f;

    l.origin    = Vec2(-0.5f * width, bottom_y_offset);
    l.direction = Vec2(width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(-0.5f * width, bottom_y_offset - height);
    l.direction = Vec2(width, 0.0f);
    renderer.push(l);

    l.origin    = Vec2(-0.5f * width, bottom_y_offset);
    l.direction = Vec2(0.0f, -height);
    renderer.push(l);

    l.origin    = Vec2(0.5f * width, bottom_y_offset);
    l.direction = Vec2(0.0f, -height);
    renderer.push(l);
  }
}

void GuiLinesUpdate::operator()(LinesRenderer& renderer) const
{
  // render_constant_lines(renderer);
  const Vec4 green_color = Vec4(Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f), 0.9f);

  Line l;
  l.color = green_color;

  //
  // Speed measuring ruler
  //

  l.width = size::Small;
  for (int i = 0; i < 25; ++i)
  {
    const float distance_between_rulers = 0.04f;
    const bool  is_longer_line          = (0 == (i % 5));
    const float upper_y_limit           = -0.7f;

    l.origin    = Vec2(-0.328f, -0.18f + (4.0f * player_speed) - (distance_between_rulers * i));
    l.direction = Vec2(is_longer_line ? -0.04f : -0.02f, 0.0f);

    if (l.origin.y < upper_y_limit)
    {
      break;
    }
    else
    {
      renderer.push(l);
    }
  }

  //
  // RED - TINY
  //

  const float red_x_offset                 = 0.02f;
  const float height_ruler_length          = 0.04f;
  const float height_ruler_left_x_position = max_left_x + ruler_lid_length + red_x_offset;

  l.width = size::Tiny;
  l.color = Vec4(1.0f, 0.0f, 0.0f, 0.9f);
  for (int side = 0; side < 2; ++side)
  {
    for (int i = 0; i < 5; ++i)
    {
      const float side_mod = (0 < side) ? -1.0f : 1.0f;

      Vec2 base_offset = Vec2(side_mod * height_ruler_left_x_position, player_y_location_meters / 8.0f);

      //
      // endless repetition
      //

      while (base_offset.y > -0.5f)
      {
        base_offset.y -= 0.8f;
      }

      while (base_offset.y < -1.2f)
      {
          base_offset.y += 0.8f;
      }

      const Vec2 size   = Vec2(side_mod * height_ruler_length, 0.2f);
      const Vec2 offset = base_offset + Vec2(0.0f, i * 0.4f);

      l.origin    = Vec2(offset.x, offset.y + (0.5f * size.y));
      l.direction = Vec2(size.x, 0.0f);
      renderer.push(l);

      l.origin    = Vec2(offset.x, offset.y + (0.5f * size.y));
      l.direction = Vec2(0.0f, -size.y);
      renderer.push(l);

      l.origin    = Vec2(offset.x, offset.y - (0.5f * size.y));
      l.direction = Vec2(size.x, 0.0f);
      renderer.push(l);
    }
  }

  //
  // YELLOW - SMALL
  //

  l.color = Vec4(1.0f, 1.0f, 0.0f, 0.7f);
  l.width = size::Small;

  for (int i = 0; i < 7; ++i)
  {
    const float distance_from_main = 0.16f;
    const float horizontal_offset  = 0.4f;

    const float x_left  = (max_left_x + ruler_lid_length + distance_from_main);
    const float x_right = -x_left;
    const float y       = -offset_up + (i * horizontal_offset) - (2 * horizontal_offset) + camera_y_pitch_radians;

    float rotation_matrix[] = {SDL_cosf(camera_x_pitch_radians), -1.0f * SDL_sinf(camera_x_pitch_radians),
                               SDL_sinf(camera_x_pitch_radians), SDL_cosf(camera_x_pitch_radians)};

    // l.origin    = Vec2(x_left * rotation_matrix[0] + y * rotation_matrix[2], x_left * rotation_matrix[1] + y *
    // rotation_matrix[3]);

    const float length = 2.0f * distance_from_main;

    l.origin    = Vec2(-0.5f * length, y);
    l.direction = Vec2(length, 0.0f);

    renderer.push(l);
  }
}
