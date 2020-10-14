#pragma once

//
// Intermediate layer responsible for hiding C-style api.
//

#include "memory_allocator.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan_core.h>

enum class RuntimeValidation
{
  Disabled,
  Enabled
};

enum class PhysicalDeviceSelectionStrategy
{
  SelectFirst
};

struct InstanceConf
{
  RuntimeValidation validation;
  const char*       name;
  SDL_Window*       window;
};

struct DeviceConf
{
  VkPhysicalDevice  physical_device;
  uint32_t          graphics_family_index;
  RuntimeValidation validation;
  bool              renderdoc_extension_active;
};

VkInstance               CreateInstance(const InstanceConf& conf, MemoryAllocator& allocator);
VkDebugUtilsMessengerEXT CreateDebugUtilsMessenger(VkInstance instance);
VkPhysicalDevice         SelectPhysicalDevice(VkInstance instance, PhysicalDeviceSelectionStrategy strategy,
                                              MemoryAllocator& allocator);
uint32_t SelectGraphicsFamilyIndex(VkPhysicalDevice physical_device, VkSurfaceKHR surface, MemoryAllocator& allocator);
bool     IsRenderdocSupported(VkPhysicalDevice physical_device, MemoryAllocator& allocator);
VkDevice CreateDevice(const DeviceConf& conf, MemoryAllocator& allocator);
