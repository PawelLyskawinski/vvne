#pragma once

#include "engine/engine.hh"
#include "engine/math.hh"

struct TerrainVertex
{
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};

//
// Calculates vertex count required to support big groups of squere patches.
// Assumes no indexing - will generate lots of duplicated vertices
//
// 1 layer
// 1 -- 1 -- 1
// |    |    |
// 1 -- 0 -- 1
// |    |    |
// 1 -- 1 -- 1
//
// 2 layers
// 2 -- 2 -- 2 -- 2 -- 2
// |    |    |    |    |
// 2 -- 1 -- 1 -- 1 -- 2
// |    |    |    |    |
// 2 -- 1 -- 0 -- 1 -- 2
// |    |    |    |    |
// 2 -- 1 -- 1 -- 1 -- 2
// |    |    |    |    |
// 2 -- 2 -- 2 -- 2 -- 2
//
uint32_t tesellated_patches_nonindexed_calculate_count(uint32_t layers);
void tesellated_patches_nonindexed_generate(uint32_t layers, float patch_dimention, TerrainVertex verts[]);
