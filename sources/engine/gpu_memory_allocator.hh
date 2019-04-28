#pragma once

#include <vulkan/vulkan.h>

//
// Actual managed remote memory:
// |-----------------------------------------------|
// | allocation    | free  | allocation    | free  |
// |-----------------------------------------------|
// 0   1   2   3   4   5   6   7   8   9  10  11  12
//
// GpuMemoryAllocator
// Nodes:
// [0] offset: 4  size: 2
// [1] offset: 10 size: 2
//
// Since it'd be hard to implement real free list allocator with remote device, this is the next closest thing.
//

struct GpuMemoryAllocator
{
  enum
  {
    MAX_FREE_BLOCKS_TRACKED = 128
  };

  struct Node
  {
    VkDeviceSize offset;
    VkDeviceSize size;
  };

  Node         nodes[MAX_FREE_BLOCKS_TRACKED];
  uint32_t     nodes_count;
  VkDeviceSize max_size;

  void         init(VkDeviceSize max_size);
  void reset();
  VkDeviceSize allocate_bytes(VkDeviceSize size);
  void         free_bytes(VkDeviceSize offset, VkDeviceSize size);
};
