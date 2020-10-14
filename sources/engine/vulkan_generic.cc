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
  SDL_assert(instance);

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
      .pfnUserCallback = vulkan_debug_callback,
  };

  ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
  ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

  ci.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  ci.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  ci.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

  auto fcn = (PFN_vkCreateDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  SDL_assert(fcn);

  VkDebugUtilsMessengerEXT debug_callback = VK_NULL_HANDLE;
  fcn(instance, &ci, nullptr, &debug_callback);
  return debug_callback;
}

VkPhysicalDevice SelectPhysicalDevice(VkInstance instance, PhysicalDeviceSelectionStrategy strategy,
                                      MemoryAllocator& allocator)
{
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  const uint64_t    handles_size = sizeof(VkPhysicalDevice) * count;
  VkPhysicalDevice* handles      = reinterpret_cast<VkPhysicalDevice*>(allocator.Allocate(handles_size));
  vkEnumeratePhysicalDevices(instance, &count, handles);

  VkPhysicalDevice selection = VK_NULL_HANDLE;

  switch (strategy)
  {
  case PhysicalDeviceSelectionStrategy::SelectFirst:
    selection = handles[0];
    break;
  }

  allocator.Free(handles, handles_size);
  return selection;
}

uint32_t SelectGraphicsFamilyIndex(VkPhysicalDevice physical_device, VkSurfaceKHR surface, MemoryAllocator& allocator)
{
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
  using Properties                   = VkQueueFamilyProperties;
  const uint64_t all_properties_size = sizeof(Properties) * count;
  Properties*    all_properties      = reinterpret_cast<Properties*>(allocator.Allocate(all_properties_size));
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, all_properties);

  auto IsSuitable = [physical_device, surface](uint32_t idx, const Properties& properties) {
    VkBool32 has_present_support = 0;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, idx, surface, &has_present_support);
    return (has_present_support && (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT));
  };

  uint32_t result = UINT32_MAX;
  for (uint32_t i = 0; i < count; ++i)
  {
    if (IsSuitable(i, all_properties[i]))
    {
      result = i;
      break;
    }
  }

  allocator.Free(all_properties, all_properties_size);
  SDL_assert(UINT32_MAX != result);
  return result;
}

bool IsRenderdocSupported(VkPhysicalDevice physical_device, MemoryAllocator& allocator)
{
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
  using Properties                   = VkExtensionProperties;
  const uint64_t all_properties_size = sizeof(Properties) * count;
  Properties*    all_properties      = reinterpret_cast<Properties*>(allocator.Allocate(all_properties_size));
  vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, all_properties);

  auto Matcher = [](const VkExtensionProperties& p) {
    SDL_Log("[rdoc] %s", p.extensionName);
    return 0 == SDL_strcmp(p.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
  };

  bool result = false;
  for (uint32_t i = 0; i < count; ++i)
  {
    if (Matcher(all_properties[i]))
    {
      result = true;
      break;
    }
  }

  SDL_Log("Renderdoc: %s", result ? "True" : "False");
  allocator.Free(all_properties, all_properties_size);
  return result;
}

VkDevice CreateDevice(const DeviceConf& conf, MemoryAllocator& allocator)
{
  const char* device_layers[]     = {"VK_LAYER_KHRONOS_validation"};
  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_EXTENSION_NAME};
  float       queue_priorities[]  = {1.0f};

  VkDeviceQueueCreateInfo graphics = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = conf.graphics_family_index,
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
  if (conf.renderdoc_extension_active)
  {
    device_extensions_count += 1;
  }

  VkDeviceCreateInfo ci = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount    = 1,
      .pQueueCreateInfos       = &graphics,
      .enabledLayerCount       = 0u,
      .ppEnabledLayerNames     = nullptr,
      .enabledExtensionCount   = device_extensions_count,
      .ppEnabledExtensionNames = device_extensions,
      .pEnabledFeatures        = &device_features,
  };

  if (RuntimeValidation::Enabled == conf.validation)
  {
    ci.enabledLayerCount   = static_cast<uint32_t>(SDL_arraysize(device_layers));
    ci.ppEnabledLayerNames = device_layers;
  }

  VkDevice device = VK_NULL_HANDLE;
  vkCreateDevice(conf.physical_device, &ci, nullptr, &device);
  return device;
}

RenderdocFunctions LoadRenderdocFunctions(VkDevice device)
{
  RenderdocFunctions r;

  auto Get = [device](const char* name) { return vkGetDeviceProcAddr(device, name); };

  r.set_object_tag  = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>(Get("vkDebugMarkerSetObjectTagEXT"));
  r.set_object_name = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(Get("vkDebugMarkerSetObjectNameEXT"));
  r.begin           = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(Get("vkCmdDebugMarkerBeginEXT"));
  r.end             = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(Get("vkCmdDebugMarkerEndEXT"));
  r.insert          = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>(Get("vkCmdDebugMarkerInsertEXT"));

  return r;
}
