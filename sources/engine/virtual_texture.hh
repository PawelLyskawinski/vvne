#pragma once

#include "vtl/align.hh"
#include "vtl/multibitfield64.hh"
#include <vulkan/vulkan.h>

constexpr uint32_t calculate_page_size_exponential_mips(uint32_t mips)
{
  uint32_t size = 1u;
  for (uint32_t i = 0; i < mips; ++i)
  {
    size *= 2.0f;
  }
  return size;
}

struct VirtualTexture
{
  static constexpr uint32_t mips_count        = 8;
  static constexpr uint32_t pages_host_x      = 50;
  static constexpr uint32_t pages_host_y      = pages_host_x;
  static constexpr uint32_t pages_host_count  = pages_host_x * pages_host_y;
  static constexpr uint32_t bytes_per_pixel   = 4; // RGBA32

  //
  // Row major layout indexing
  //  _______________
  // | 0 | 1 | 2 | 3 |
  // | 4 | 5 | 6 | 7 |
  // |___|___|___|___|
  //
  static constexpr uint32_t usage_bitfield_size = (pages_host_count / 64u) + 1;

  void     debug_dump() const;
  uint32_t calculate_all_required_memory() const;

  MultiBitfield64<usage_bitfield_size> usage;
};
