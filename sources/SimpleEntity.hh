#pragma once

#include "engine/free_list_allocator.hh"
#include "engine/gltf.hh"

struct SimpleEntity
{
  void init(FreeListAllocator& allocator, const SceneGraph& model);
  void recalculate_node_transforms(const SceneGraph& model, const Mat4x4& world_transform);
  void animate(FreeListAllocator& allocator, const SceneGraph& scene_graph, float current_time_sec);

  // elements which will always be guaranteed to be present for entity
  uint8_t* node_parent_hierarchy;
  Mat4x4*  node_transforms;
  Mat4x4*  joint_matrices;

  // initialized at first usage in animation system
  Quaternion* node_rotations;
  Vec3*       node_translations;

  // value state
  uint64_t node_renderabilities;
  uint64_t node_anim_rotation_applicability;
  uint64_t node_anim_translation_applicability;
  float    animation_start_time;

  struct Flags
  {
    bool rotations : 1;
    bool translations : 1;
    bool anim_rotation_applicability : 1;
    bool anim_translation_applicability : 1;
    bool animation_start_time : 1;
  };

  Flags flags;
};
