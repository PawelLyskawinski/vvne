#pragma once

struct VkPhysicalDevice_T;
struct VkSurfaceKHR_T;
struct FreeListAllocator;

unsigned select_graphics_family_index(VkPhysicalDevice_T* physical_device, VkSurfaceKHR_T* surface,
                                      FreeListAllocator* allocator);