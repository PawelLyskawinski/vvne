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

struct GuiLineSizeCount
{
  int big;
  int normal;
  int small;
  int tiny;
};

void generate_gui_lines(const GenerateGuiLinesCommand& cmd, Vec2 dst[], uint32_t dst_capacity,
                        GuiLineSizeCount& green_counter, GuiLineSizeCount& red_counter,
                        GuiLineSizeCount& yellow_counter);

struct GuiHeightRulerText
{
  Vec2 offset;
  int  size  = 0;
  int  value = 0;
};

ArrayView<GuiHeightRulerText> generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator);
ArrayView<GuiHeightRulerText> generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, Stack& allocator);
