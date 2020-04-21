#pragma once

#include "hierarchical_allocator.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan_core.h>

struct InstanceCreateInfo
{
  const char*            application_name   = nullptr;
  const char*            engine_name        = nullptr;
  SDL_Window*            window             = nullptr;
  HierarchicalAllocator* allocator          = nullptr;
  bool                   validation_enabled = false;
};

VkInstance instance_create(const InstanceCreateInfo& info);
