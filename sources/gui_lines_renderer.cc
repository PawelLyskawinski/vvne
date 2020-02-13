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

} // namespace

void GuiLinesUpdate::operator()(LinesRenderer& renderer) const
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
}
