#pragma once

#include "engine/math.hh"
#include "vtl/allocators.hh"
#include "vtl/span.hh"
#include <vulkan/vulkan.h>

struct GuiText
{
  Vec2 offset = {};
  Vec3 color  = {};
  int  size   = 0;
  int  value  = 0;
};

struct GuiTextGenerator
{
  float      player_y_location_meters;
  float      camera_x_pitch_radians;
  float      camera_y_pitch_radians;
  VkExtent2D screen_extent2D;

  [[nodiscard]] Span<GuiText> height_ruler(Stack& allocator) const;
  [[nodiscard]] Span<GuiText> tilt_ruler(Stack& allocator) const;
};
