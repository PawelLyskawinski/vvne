#pragma once

#include <vulkan/vulkan_core.h>
#include "engine/math.hh"

struct LinesRenderer;

//
// Part of gui is always the same no matter what.
// Those lines can be cached once and then reused each next frame
//
void render_constant_lines(LinesRenderer& renderer);

struct GuiLinesUpdate
{
  float      player_y_location_meters = 0.0f;
  float      camera_x_pitch_radians   = 0.0f;
  float      camera_y_pitch_radians   = 0.0f;
  float      player_speed             = 0.0f;
  Vec2       debug                    = {};
  VkExtent2D screen_extent2D          = {};

  void operator()(LinesRenderer& renderer) const;
};