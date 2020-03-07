#pragma once

#include <vulkan/vulkan.h>

struct VirtualTexture
{
  static constexpr uint32_t page_dimension_pix   = 256;
  static constexpr uint32_t page_pixel_count     = page_dimension_pix * page_dimension_pix;
  static constexpr uint32_t pages_on_x_in_mip_0  = 50;
  static constexpr uint32_t pages_on_y_in_mip_0  = pages_on_x_in_mip_0;
  static constexpr uint32_t pages_count_in_mip_0 = pages_on_x_in_mip_0 * pages_on_y_in_mip_0;
  static constexpr uint32_t bytes_per_pixel      = 4; // RGBA32
  static constexpr uint32_t mip_0_memory         = pages_count_in_mip_0 * page_pixel_count * bytes_per_pixel;

  VkDeviceMemory memory;
};

//
// Points from texture coordinates of specific MIP to physical memory on GPU
//
//

struct VirtualPageTable
{
};
