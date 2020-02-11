#pragma once

#include "engine/hierarchical_allocator.hh"
#include "engine/math.hh"
#include <vulkan/vulkan_core.h>

struct Line
{
  Vec2  origin;
  Vec2  size;
  Vec4  color;
  float width; // 7.0f is usually safe supported max across different gpu vendors

  bool operator<(const Line& rhs) const;
};

struct LinesRenderer
{
  static constexpr uint32_t lines_capacity          = 256;
  static constexpr uint32_t position_cache_capacity = lines_capacity * 2;

  void setup(HierarchicalAllocator& allocator);
  void teardown(HierarchicalAllocator& allocator);
  void cache_lines();
  void render(VkCommandBuffer cmd, VkPipelineLayout layout);

  Line*    lines;
  Vec2*    position_cache;
  uint32_t lines_size;
  uint32_t position_cache_size;
};