#pragma once

#include <vulkan/vulkan_core.h>

struct LinesRenderer;

struct GuiLinesUpdate
{
  float      player_y_location_meters = 0.0f;
  float      camera_x_pitch_radians   = 0.0f;
  float      camera_y_pitch_radians   = 0.0f;
  VkExtent2D screen_extent2D          = {};

  void operator()(LinesRenderer& renderer) const;
};