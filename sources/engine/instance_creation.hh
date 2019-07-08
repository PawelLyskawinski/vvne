#pragma once

struct VkInstance_T;
struct SDL_Window;
struct FreeListAllocator;

//
// Creates vulkan context/instance
//

VkInstance_T* instance_create(SDL_Window* window, FreeListAllocator* allocator, bool enable_validation);