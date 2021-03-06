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

enum class SurfaceFormatSelectionStrategy
{
  PreferSRGBnonlinearBGRA8,
};

enum class PresentModeSelectionStrategy
{
  PreferImmediate,
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

struct RenderdocFunctions
{
  PFN_vkDebugMarkerSetObjectTagEXT  set_object_tag;
  PFN_vkDebugMarkerSetObjectNameEXT set_object_name;
  PFN_vkCmdDebugMarkerBeginEXT      begin;
  PFN_vkCmdDebugMarkerEndEXT        end;
  PFN_vkCmdDebugMarkerInsertEXT     insert;
};

struct SwapchainConf
{
  VkSurfaceKHR                  surface;
  VkSurfaceFormatKHR            surface_format;
  VkExtent2D                    extent;
  VkSurfaceTransformFlagBitsKHR transform;
  VkPresentModeKHR              present_mode;
  uint32_t                      count;
};

enum class ImageType
{
  MSAAResolve,
  DepthTest,
  CascadeShadowMap,
};

struct ImageConf
{
  ImageType             type;
  VkFormat              format;
  VkExtent2D            extent;
  uint32_t              layers;
  VkSampleCountFlagBits sample_count;
};

VkInstance               CreateInstance(const InstanceConf& conf, MemoryAllocator& allocator);
VkDebugUtilsMessengerEXT CreateDebugUtilsMessenger(VkInstance instance);
VkPhysicalDevice         SelectPhysicalDevice(VkInstance instance, PhysicalDeviceSelectionStrategy strategy,
                                              MemoryAllocator& allocator);
uint32_t SelectGraphicsFamilyIndex(VkPhysicalDevice physical_device, VkSurfaceKHR surface, MemoryAllocator& allocator);
bool     IsRenderdocSupported(VkPhysicalDevice physical_device, MemoryAllocator& allocator);
VkDevice CreateDevice(const DeviceConf& conf, MemoryAllocator& allocator);
RenderdocFunctions LoadRenderdocFunctions(VkDevice device);
VkSurfaceFormatKHR SelectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                       SurfaceFormatSelectionStrategy strategy, MemoryAllocator& allocator);
VkPresentModeKHR   SelectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                     PresentModeSelectionStrategy strategy, MemoryAllocator& allocator);
VkSwapchainKHR     CreateSwapchain(VkDevice device, const SwapchainConf& conf);
VkImage            CreateImage(VkDevice device, const ImageConf& conf);
VkSampler CreateSampler(VkDevice device, VkSamplerAddressMode address_mode, VkBorderColor border_color);

