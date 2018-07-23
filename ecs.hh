#pragma once

#include <linmath.h>

struct ComponentBitfield
{
public:
  int allocate()
  {
    for (int batch_idx = 0; batch_idx < (int)SDL_arraysize(usage); ++batch_idx)
    {
      const uint64_t current_batch = usage[batch_idx];

      if (UINT64_MAX == current_batch)
        continue;

      for (int offset = 0; offset < 64; ++offset)
      {
        const uint64_t mask = (1u << offset);

        if (current_batch & mask)
          continue;

        usage[batch_idx] |= mask;
        return 64 * batch_idx + offset;
      }
    }

    return 0;
  }

  void free(int i)
  {
    usage[i / 64] &= ~(1 << (i % 64));
  }

private:
  uint64_t usage[4];
};

struct AnimationTranslation
{
  vec3     animations[64];
  uint64_t applicability;
};

struct AnimationRotation
{
  quat     rotations[64];
  uint64_t applicability;
};

struct NodeParentHierarchy
{
  uint8_t hierarchy[64];
};

struct NodeTransforms
{
  mat4x4 transforms[64];
};

struct JointMatrices
{
  mat4x4 joints[64];
};

struct EntityComponentSystem
{
  ComponentBitfield animation_translations_usage;
  ComponentBitfield animation_rotations_usage;
  ComponentBitfield animation_start_times_usage;
  ComponentBitfield node_parent_hierarchies_usage;
  ComponentBitfield node_renderabilities_usage;
  ComponentBitfield node_transforms_usage;
  ComponentBitfield joint_matrices_usage;

  AnimationTranslation animation_translations[64];
  AnimationRotation    animation_rotations[64];
  float                animation_start_times[64];
  NodeParentHierarchy  node_parent_hierarchies[64];
  uint64_t             node_renderabilities[64];
  NodeTransforms       node_transforms[64];
  JointMatrices        joint_matrices[64];
};

struct Entity
{
  int animation_translation;
  int animation_rotation;
  int animation_start_time;
  int node_parent_hierarchy;
  int node_renderabilities;
  int node_transforms;
  int joint_matrices;

  void reset()
  {
    animation_translation = -1;
    animation_rotation    = -1;
    animation_start_time  = -1;
    node_parent_hierarchy = -1;
    node_renderabilities  = -1;
    node_transforms       = -1;
    joint_matrices        = -1;
  }
};
