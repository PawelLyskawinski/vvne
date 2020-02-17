#pragma once

#include "engine/allocators.hh"
#include "engine/math.hh"
#include <vulkan/vulkan.h>

struct GenerateGuiLinesCommand
{
  float      player_y_location_meters;
  float      camera_x_pitch_radians;
  float      camera_y_pitch_radians;
  VkExtent2D screen_extent2D;
};

struct GuiHeightRulerText
{
  Vec2 offset = {};
  int  size   = 0;
  int  value  = 0;
};

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator);
ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator);
