#include "vulkan_generic.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
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

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                            VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                            void*                                       pUserData)
{
  (void)messageType;
  (void)pUserData;
  SDL_Log("[%u]: %s", messageSeverity, pCallbackData->pMessage);
  return VK_FALSE;
}

VkDebugUtilsMessengerEXT CreateDebugUtilsMessenger(VkInstance instance)
{
  VkDebugUtilsMessengerCreateInfoEXT ci = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = vulkan_debug_callback,
  };

  auto fcn = (PFN_vkCreateDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  SDL_assert(fcn);

  VkDebugUtilsMessengerEXT debug_callback = VK_NULL_HANDLE;
  fcn(instance, &ci, nullptr, &debug_callback);
  return debug_callback;
}
