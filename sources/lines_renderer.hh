#pragma once

#include "engine/memory_allocator.hh"
#include "engine/math.hh"
#include <vulkan/vulkan_core.h>
#include <SDL2/SDL_assert.h>

struct Line
{
  Vec2  origin;
  Vec2  direction;
  Vec4  color;
  float width; // 7.0f is usually safe supported max across different gpu vendors
  bool  operator<(const Line& rhs) const;
};

struct LinesRenderer
{
  void setup(MemoryAllocator& allocator, uint32_t capacity);
  void teardown(MemoryAllocator& allocator);
  void cache_lines(MemoryAllocator& allocator);
  //void cache_lines(Line* tmp_helper_space);
  void reset();
  void render(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t base_offset = 0) const;

  void push(const Line& line)
  {
    SDL_assert(lines_capacity != lines_size);
    lines[lines_size++] = line;
  }

  Line*    lines;
  Vec2*    position_cache;
  uint32_t lines_size;
  uint32_t position_cache_size;
  uint32_t lines_capacity;
};