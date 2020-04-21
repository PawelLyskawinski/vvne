#include "engine_instance_create.hh"
#include <SDL2/SDL_vulkan.h>

VkInstance instance_create(const InstanceCreateInfo& info)
{
  VkApplicationInfo ai = {
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = info.application_name,
      .applicationVersion = 1,
      .pEngineName        = info.engine_name,
      .engineVersion      = 1,
      .apiVersion         = VK_API_VERSION_1_0,
  };

  const char* validation_layers[]     = {"VK_LAYER_KHRONOS_validation"};
  const char* validation_extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  uint32_t count = 0;
  SDL_Vulkan_GetInstanceExtensions(info.window, &count, nullptr);
  const uint32_t allocation_size = count + SDL_arraysize(validation_extensions);
  const char**   extensions      = info.allocator->allocate<const char*>(allocation_size);
  SDL_Vulkan_GetInstanceExtensions(info.window, &count, extensions);

  if (info.validation_enabled)
  {
    for (const char* extension : validation_extensions)
    {
      extensions[count++] = extension;
    }
  }

  VkInstanceCreateInfo ci = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &ai,
      .enabledLayerCount       = info.validation_enabled ? static_cast<uint32_t>(SDL_arraysize(validation_layers)) : 0u,
      .ppEnabledLayerNames     = info.validation_enabled ? validation_layers : nullptr,
      .enabledExtensionCount   = count,
      .ppEnabledExtensionNames = extensions,
  };

  VkInstance r = VK_NULL_HANDLE;
  vkCreateInstance(&ci, nullptr, &r);
  info.allocator->free(extensions, allocation_size);
  return r;
}
