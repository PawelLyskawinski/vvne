#pragma once

struct VkDevice_T;
struct VkPhysicalDevice_T;
struct FreeListAllocator;

bool verify_physical_device_extension(VkPhysicalDevice_T* physical_device, FreeListAllocator* allocator,
                                      const char* name);

VkDevice_T* device_create(VkPhysicalDevice_T* physical_device, unsigned graphics_family_index, bool validation_enabled,
                          bool renderdoc_marker_naming_enabled);