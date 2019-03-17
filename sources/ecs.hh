#pragma once

#include "engine/gltf.hh"
#include <linmath.h>

struct Incrementable
{
public:
  int increment(int inc_val)
  {
    int r = atomic_value;
    atomic_value += inc_val;
    return r;
  }

  float usage_percent(int total) const { return (float)atomic_value / (float)total; }

private:
  int atomic_value;
};

struct Ecs
{
  mat4x4  joint_matrices[512];
  mat4x4  node_transforms[512];
  uint8_t node_hierarchy[512];
  quat    node_anim_rotations[512];
  vec3    node_anim_translations[512];

  Incrementable joint_matrices_stack;
  Incrementable node_transforms_stack;
  Incrementable node_hierarchy_stack;
  Incrementable node_anim_rotations_stack;
  Incrementable node_anim_translations_stack;
};

struct SimpleEntity
{
  void init(Ecs& ecs, const SceneGraph& model);
  void recalculate_node_transforms(Ecs& ecs, const SceneGraph& model, mat4x4 world_transform);

  // elements which will always be guaranteed to be present for entity
  int node_parent_hierarchy;
  int node_transforms;

  // initialized at first usage in animation system
  int node_rotations;
  int node_translations;

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

struct SkinnedEntity
{
  void init(Ecs& ecs, const SceneGraph& model);
  void recalculate_skinning_matrices(Ecs& ecs, const SceneGraph& scene_graph, mat4x4 world_transform);

  SimpleEntity base;
  int          joint_matrices;
};
