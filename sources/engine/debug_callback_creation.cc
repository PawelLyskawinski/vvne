#include "debug_callback_creation.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <vulkan/vulkan.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                            VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                            void*                                       pUserData)
{
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;
  SDL_Log("validation layer: %s", pCallbackData->pMessage);
  return VK_FALSE;
}

VkDebugUtilsMessengerEXT debug_callback_create(VkInstance instance)
{
  constexpr VkDebugUtilsMessageSeverityFlagsEXT severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

  constexpr VkDebugUtilsMessageTypeFlagsEXT supported_types = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

  VkDebugUtilsMessengerCreateInfoEXT ci = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = severity,
      .messageType     = supported_types,
      .pfnUserCallback = vulkan_debug_callback,
  };

  const char* function_name = "vkCreateDebugUtilsMessengerEXT";
  using function_type       = PFN_vkCreateDebugUtilsMessengerEXT;
  auto fcn                  = reinterpret_cast<function_type>(vkGetInstanceProcAddr(instance, function_name));

  SDL_assert(fcn);

  VkDebugUtilsMessengerEXT debug_callback = VK_NULL_HANDLE;
  fcn(instance, &ci, nullptr, &debug_callback);
  return debug_callback;
}
