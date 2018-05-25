#pragma once

#include "engine.hh"

// allocates on front stack the tile positions

struct LevelLoadResult
{
  VkDeviceSize vertex_target_offset;
  VkDeviceSize index_target_offset;
  int          index_count;
  VkIndexType  index_type;
};

struct VrLevelLoadResult
{
  float           entrance_point[2];
  float           target_goal[2];
  LevelLoadResult level_load_data;
};

VrLevelLoadResult level_generator_vr(Engine* engine);
