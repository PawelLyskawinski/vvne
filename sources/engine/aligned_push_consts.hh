#pragma once

#include <vulkan/vulkan_core.h>

class AlignedPushConsts
{
public:
  AlignedPushConsts(VkCommandBuffer command, VkPipelineLayout layout)
      : command(command)
      , layout(layout)
      , offset(0)
  {
  }

  AlignedPushConsts& push(const VkShaderStageFlags stage, const size_t size, const void* data)
  {
    vkCmdPushConstants(command, layout, stage, offset, size, data);
    offset += size;
    return *this;
  }

  template <typename T> AlignedPushConsts& push(const VkShaderStageFlags stage, const T& data)
  {
    return push(stage, sizeof(T), &data);
  }

private:
  VkCommandBuffer  command;
  VkPipelineLayout layout;
  size_t           offset;
};
