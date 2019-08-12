#pragma once

#include "engine/free_list_allocator.hh"
#include "engine/gltf.hh"
#include <linmath.h>

struct SimpleEntity
{
  void init(FreeListAllocator& allocator, const SceneGraph& model);
  void recalculate_node_transforms(const SceneGraph& model, mat4x4 world_transform);

  // elements which will always be guaranteed to be present for entity
  mat4x4*  node_transforms;
  mat4x4*  joint_matrices;

  // initialized at first usage in animation system
  quat* node_rotations;
  vec3* node_translations;

  // value state
  uint64_t node_renderabilities;
  uint64_t node_anim_rotation_applicability;
  uint64_t node_anim_translation_applicability;
  float    animation_start_time;

  enum Property
  {
    NodeRotations                    = (1 << 0),
    NodeTranslations                 = (1 << 1),
    NodeAnimRotationApplicability    = (1 << 2),
    NodeAnimTranslationApplicability = (1 << 3),
    AnimationStartTime               = (1 << 4),
  };

  uint64_t flags;
};
