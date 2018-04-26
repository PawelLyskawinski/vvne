#pragma once

#include "engine.hh"
#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

namespace gltf {

struct RenderableModel
{
  VkDeviceSize indices_offset;
  VkDeviceSize vertices_offset;
  VkIndexType  indices_type;
  uint32_t     indices_count;

  int albedo_texture_idx;
  int metal_roughness_texture_idx;
  int emissive_texture_idx;
  int AO_texture_idx;
  int normal_texture_idx;

  void loadGLB(Engine& engine, const char* path) noexcept;
};

}; // namespace gltf
