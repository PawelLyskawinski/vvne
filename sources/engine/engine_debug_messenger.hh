#pragma once

#include <vulkan/vulkan_core.h>

VkDebugUtilsMessengerEXT debug_utils_messenger_create(VkInstance instance);
void debug_utils_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT handle);
