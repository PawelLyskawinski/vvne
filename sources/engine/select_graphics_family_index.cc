#include "select_graphics_family_index.hh"
#include "free_list_allocator.hh"
#include <vulkan/vulkan.h>

uint32_t select_graphics_family_index(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                      FreeListAllocator* allocator)
{
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
  VkQueueFamilyProperties* all_properties = allocator->allocate<VkQueueFamilyProperties>(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, all_properties);

  uint32_t graphics_family_index = 0;

  for (uint32_t i = 0; i < count; ++i)
  {
    VkQueueFamilyProperties properties = all_properties[i];

    VkBool32 has_present_support = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &has_present_support);

    if (has_present_support && (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT))
    {
      graphics_family_index = i;
      break;
    }
  }

  allocator->free(all_properties, count);
  return graphics_family_index;
}
