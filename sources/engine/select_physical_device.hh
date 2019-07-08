#pragma once

struct VkPhysicalDevice_T;
struct VkInstance_T;
struct FreeListAllocator;

//
// Selects most suitable physical device (most likely graphics card, not integrated)
//

VkPhysicalDevice_T* select_physical_device(VkInstance_T* instance, FreeListAllocator* allocator);