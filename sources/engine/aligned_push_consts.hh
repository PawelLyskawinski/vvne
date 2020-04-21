#pragma once

#include "vtl/span.hh"
#include <vulkan/vulkan_core.h>

struct AlignedPushConstsContext
{
  VkCommandBuffer  command = VK_NULL_HANDLE;
  VkPipelineLayout layout  = VK_NULL_HANDLE;
};

struct AlignedPushElement
{
  VkShaderStageFlags stage = 0;
  size_t             size  = 0;
  const void*        data  = nullptr;
};

void push_constants(AlignedPushConstsContext ctx, Span<AlignedPushElement> elements);
