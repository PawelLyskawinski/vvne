#pragma once

#include "engine/free_list_allocator.hh"
#include "engine/gltf.hh"

struct SimpleEntity
{
  void init(FreeListAllocator& allocator, const SceneGraph& model);
  void recalculate_node_transforms(const SceneGraph& model, Mat4x4 world_transform);

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

  enum Property : uint64_t
  {
    NodeRotations                    = uint64_t(1) << 0u,
    NodeTranslations                 = uint64_t(1) << 1u,
    NodeAnimRotationApplicability    = uint64_t(1) << 2u,
    NodeAnimTranslationApplicability = uint64_t(1) << 3u,
    AnimationStartTime               = uint64_t(1) << 4u,
  };

  uint64_t flags;
};
