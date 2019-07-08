#include "select_physical_device.hh"
#include "free_list_allocator.hh"
#include <vulkan/vulkan.h>

VkPhysicalDevice select_physical_device(VkInstance instance, FreeListAllocator* allocator)
{
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  VkPhysicalDevice* handles = allocator->allocate<VkPhysicalDevice>(count);
  vkEnumeratePhysicalDevices(instance, &count, handles);

  VkPhysicalDevice physical_device = handles[0];
  allocator->free(handles, count);
  return physical_device;
}