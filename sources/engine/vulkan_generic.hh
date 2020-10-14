#pragma once

//
// Intermediate layer responsible for hiding C-style api.
//

#include "memory_allocator.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan_core.h>

enum class RuntimeValidation
{
  Disabled,
  Enabled
};

struct InstanceConf
{
  RuntimeValidation validation;
  const char*       name;
  SDL_Window*       window;
};

VkInstance CreateInstance(const InstanceConf& conf, MemoryAllocator& allocator);
