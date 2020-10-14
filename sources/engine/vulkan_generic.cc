#include "vulkan_generic.hh"
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

VkInstance CreateInstance(const InstanceConf& conf, MemoryAllocator& allocator)
{
  //
  // engine name will be formed by adding "_engine" postfix to the provided name
  //
  const char*    engine_name_postfix    = "_engine";
  const uint64_t engine_name_buffer_len = SDL_strlen(conf.name) + SDL_strlen(engine_name_postfix) + 1;
  char*          engine_name_buffer     = reinterpret_cast<char*>(allocator.Allocate(engine_name_buffer_len));

  SDL_strlcpy(engine_name_buffer, conf.name, engine_name_buffer_len);
  SDL_strlcat(engine_name_buffer, engine_name_postfix, engine_name_buffer_len);

  VkApplicationInfo ai = {
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = conf.name,
      .applicationVersion = 1,
      .pEngineName        = engine_name_buffer,
      .engineVersion      = 1,
      .apiVersion         = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo ci = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &ai,
      .enabledLayerCount       = 0,
      .ppEnabledLayerNames     = nullptr,
      .enabledExtensionCount   = 0,
      .ppEnabledExtensionNames = nullptr,
  };

  uint32_t count = 0;
  SDL_Vulkan_GetInstanceExtensions(conf.window, &count, nullptr);
  uint64_t     extensions_size = sizeof(const char*) * count;
  const char** extensions      = reinterpret_cast<const char**>(allocator.Allocate(extensions_size));
  SDL_Vulkan_GetInstanceExtensions(conf.window, &count, extensions);

  const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

  if (RuntimeValidation::Enabled == conf.validation)
  {
    count += 1;
    extensions_size       = sizeof(const char*) * count;
    extensions            = reinterpret_cast<const char**>(allocator.Reallocate(extensions, extensions_size));
    extensions[count - 1] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    ci.enabledLayerCount   = SDL_arraysize(validation_layers);
    ci.ppEnabledLayerNames = validation_layers;
  }

  ci.enabledExtensionCount   = count;
  ci.ppEnabledExtensionNames = extensions;

  VkInstance instance = VK_NULL_HANDLE;
  vkCreateInstance(&ci, nullptr, &instance);

  allocator.Free(extensions, extensions_size);
  allocator.Free(engine_name_buffer, engine_name_buffer_len);

  return instance;
}