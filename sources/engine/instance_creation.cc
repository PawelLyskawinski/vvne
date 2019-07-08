#include "instance_creation.hh"
#include "free_list_allocator.hh"
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

VkInstance instance_create(SDL_Window* window, FreeListAllocator* allocator, bool enable_validation)
{
  VkApplicationInfo ai = {
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = "vvne",
      .applicationVersion = 1,
      .pEngineName        = "vvne_engine",
      .engineVersion      = 1,
      .apiVersion         = VK_API_VERSION_1_0,
  };

  const char* validation_layers[]     = {"VK_LAYER_KHRONOS_validation"};
  const char* validation_extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  uint32_t count = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr);
  const char** extensions = allocator->allocate<const char*>(count + SDL_arraysize(validation_extensions));
  SDL_Vulkan_GetInstanceExtensions(window, &count, extensions);

  if (enable_validation)
  {
    SDL_memcpy(&extensions[count], validation_extensions, sizeof(validation_extensions));
    count += SDL_arraysize(validation_extensions);
  }

  VkInstanceCreateInfo ci = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext                   = nullptr,
      .flags                   = 0,
      .pApplicationInfo        = &ai,
      .enabledLayerCount       = enable_validation ? static_cast<uint32_t>(SDL_arraysize(validation_layers)) : 0u,
      .ppEnabledLayerNames     = enable_validation ? validation_layers : nullptr,
      .enabledExtensionCount   = count,
      .ppEnabledExtensionNames = extensions,
  };

  VkInstance instance = VK_NULL_HANDLE;
  vkCreateInstance(&ci, nullptr, &instance);
  allocator->free(extensions, count);
  return instance;
}
