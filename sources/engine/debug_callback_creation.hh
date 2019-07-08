#pragma once

struct VkDebugUtilsMessengerEXT_T;
struct VkInstance_T;

//
// Creates vulkan validation layer message printer
//

VkDebugUtilsMessengerEXT_T* debug_callback_create(VkInstance_T* instance);
