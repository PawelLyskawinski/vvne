#include "engine_debug_messenger.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>

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

VkDebugUtilsMessengerEXT debug_utils_messenger_create(VkInstance instance)
{
  const VkDebugUtilsMessageSeverityFlagsEXT severity = VkFlags(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) |
                                                       VkFlags(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) |
                                                       VkFlags(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);

  const VkDebugUtilsMessageTypeFlagsEXT message_type = VkFlags(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) |
                                                       VkFlags(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) |
                                                       VkFlags(VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);

  VkDebugUtilsMessengerCreateInfoEXT ci = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = severity,
      .messageType     = message_type,
      .pfnUserCallback = vulkan_debug_callback,
  };

  auto fcn = (PFN_vkCreateDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  SDL_assert(fcn);

  VkDebugUtilsMessengerEXT r = VK_NULL_HANDLE;
  fcn(instance, &ci, nullptr, &r);
  return r;
}

void debug_utils_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT handle)
{
  auto fcn = (PFN_vkDestroyDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  SDL_assert(fcn);
  fcn(instance, handle, nullptr);
}
