#include "device_creation.hh"
#include "free_list_allocator.hh"
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>

bool verify_physical_device_extension(VkPhysicalDevice physical_device, FreeListAllocator* allocator, const char* name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    VkExtensionProperties* properties = allocator->allocate<VkExtensionProperties>(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, properties);

    bool result = false;
    for(uint32_t i=0; i<count; ++i)
    {
        if(0 == SDL_strcmp(properties[i].extensionName, name))
        {
            result = true;
            break;
        }
    }

    allocator->free(properties, count);
    return result;
}

VkDevice device_create(VkPhysicalDevice physical_device, unsigned graphics_family_index, bool validation_enabled,
                       bool renderdoc_marker_naming_enabled)
{
  const char* device_layers[]     = {"VK_LAYER_KHRONOS_validation"};
  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_EXTENSION_NAME};
  float       queue_priorities[]  = {1.0f};

  VkDeviceQueueCreateInfo graphics = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = graphics_family_index,
      .queueCount       = SDL_arraysize(queue_priorities),
      .pQueuePriorities = queue_priorities,
  };

  VkPhysicalDeviceFeatures device_features = {
      .tessellationShader = VK_TRUE,
      .sampleRateShading  = VK_TRUE,
      .fillModeNonSolid   = VK_TRUE, // enables VK_POLYGON_MODE_LINE
      .wideLines          = VK_TRUE,
  };

  uint32_t device_extensions_count = SDL_arraysize(device_extensions) - 1;

  if (validation_enabled and renderdoc_marker_naming_enabled)
  {
    device_extensions_count += 1;
  }

  VkDeviceCreateInfo ci = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount    = 1,
      .pQueueCreateInfos       = &graphics,
      .enabledLayerCount       = validation_enabled ? static_cast<uint32_t>(SDL_arraysize(device_layers)) : 0u,
      .ppEnabledLayerNames     = validation_enabled ? device_layers : nullptr,
      .enabledExtensionCount   = device_extensions_count,
      .ppEnabledExtensionNames = device_extensions,
      .pEnabledFeatures        = &device_features,
  };

  VkDevice device = VK_NULL_HANDLE;
  vkCreateDevice(physical_device, &ci, nullptr, &device);
  return device;
}
