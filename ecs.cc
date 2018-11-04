#include "ecs.hh"
#include <SDL2/SDL_assert.h>

namespace {

void depth_first_node_parent_hierarchy(uint8_t* hierarchy, const Node* nodes, uint8_t parent_idx, uint8_t node_idx)
{
  for (int child_idx : nodes[node_idx].children)
    depth_first_node_parent_hierarchy(hierarchy, nodes, node_idx, static_cast<uint8_t>(child_idx));
  hierarchy[node_idx] = parent_idx;
}

void propagate_node_renderability_hierarchy(int node_idx, uint64_t& dst, const ArrayView<Node>& nodes)
{
  for (int child_idx : nodes[node_idx].children)
    propagate_node_renderability_hierarchy(child_idx, dst, nodes);
  dst |= (1 << node_idx);
}

void depth_first_node_transform(mat4x4* transforms, Node* nodes, const int parent_node_idx, const int node_idx)
{
  mat4x4_mul(transforms[node_idx], transforms[parent_node_idx], transforms[node_idx]);
  for (int child_idx : nodes[node_idx].children)
    depth_first_node_transform(transforms, nodes, node_idx, child_idx);
}

} // namespace

void SimpleEntity::init(Ecs& ecs, const SceneGraph& model)
{
  const int nodes_count = model.nodes.count;
  SDL_assert(nodes_count < 64);

  node_parent_hierarchy = ecs.node_hierarchy_stack.increment(nodes_count);
  node_transforms       = ecs.node_transforms_stack.increment(nodes_count);

  for (int scene_node_idx : model.scenes[0].nodes)
    propagate_node_renderability_hierarchy(scene_node_idx, node_renderabilities, model.nodes);

  uint8_t* hierarchy = &ecs.node_hierarchy[node_parent_hierarchy];

  for (uint8_t i = 0; i < nodes_count; ++i)
    hierarchy[i] = i;

  for (uint8_t node_idx = 0; node_idx < nodes_count; ++node_idx)
    for (int child_idx : model.nodes[node_idx].children)
      depth_first_node_parent_hierarchy(hierarchy, model.nodes.data, node_idx, static_cast<uint8_t>(child_idx));
}

void SkinnedEntity::init(Ecs& ecs, const SceneGraph& model)
{
  base.init(ecs, model);
  joint_matrices = ecs.joint_matrices_stack.increment(model.skins[0].joints.count);
}

void SimpleEntity::recalculate_node_transforms(Ecs& ecs, const SceneGraph& model, mat4x4 world_transform)
{
  const uint8_t*         hierarchy = &ecs.node_hierarchy[node_parent_hierarchy];
  const ArrayView<Node>& nodes     = model.nodes;

  mat4x4 transforms[64] = {};

  for (int i = 0; i < nodes.count; ++i)
    mat4x4_identity(transforms[i]);

  for (int node_idx : model.scenes[0].nodes)
    mat4x4_dup(transforms[node_idx], world_transform);

  if (not model.skins.empty())
  {
    int skeleton_node_idx   = model.skins[0].skeleton;
    int skeleton_parent_idx = hierarchy[skeleton_node_idx];
    mat4x4_dup(transforms[skeleton_parent_idx], world_transform);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Translations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    mat4x4 translation_matrix = {};
    mat4x4_identity(translation_matrix);

    if (flags & (Property::NodeTranslations | Property::NodeAnimTranslationApplicability))
    {
      if (node_anim_translation_applicability & (uint64_t(1) << i))
      {
        const float* t = ecs.node_anim_translations[node_translations + i];
        mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
      }
      else if (nodes[i].flags & Node::Property::Translation)
      {
        const float* t = nodes[i].translation;
        mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
      }
    }
    else if (nodes[i].flags & Node::Property::Translation)
    {
      const float* t = nodes[i].translation;
      mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
    }

    mat4x4_mul(transforms[i], transforms[i], translation_matrix);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Rotations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    mat4x4 rotation_matrix = {};
    mat4x4_identity(rotation_matrix);

    if (flags & (Property::NodeRotations | Property::NodeAnimRotationApplicability))
    {
      if (node_anim_rotation_applicability & (uint64_t(1) << i))
      {
        const float* r   = ecs.node_anim_rotations[node_rotations + i];
        quat         tmp = {r[0], r[1], r[2], r[3]};
        mat4x4_from_quat(rotation_matrix, tmp);
      }
      else if (nodes[i].flags & Node::Property::Rotation)
      {
        const float* r   = nodes[i].rotation;
        quat         tmp = {r[0], r[1], r[2], r[3]};
        mat4x4_from_quat(rotation_matrix, tmp);
      }
    }
    else if (nodes[i].flags & Node::Property::Rotation)
    {
      const float* r   = nodes[i].rotation;
      quat         tmp = {r[0], r[1], r[2], r[3]};
      mat4x4_from_quat(rotation_matrix, tmp);
    }

    mat4x4_mul(transforms[i], transforms[i], rotation_matrix);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Scaling
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);

    if (nodes[i].flags & Node::Property::Scale)
    {
      const float* s = nodes[i].scale;
      mat4x4_scale_aniso(scale_matrix, scale_matrix, s[0], s[1], s[2]);
    }

    mat4x4_mul(transforms[i], transforms[i], scale_matrix);
  }

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
  {
    if (node_idx == hierarchy[node_idx])
      for (int child_idx : nodes[node_idx].children)
        depth_first_node_transform(transforms, nodes.data, node_idx, child_idx);
  }

  SDL_memcpy(&ecs.node_transforms[node_transforms], transforms, sizeof(mat4x4) * nodes.count);
}

void SkinnedEntity::recalculate_skinning_matrices(Ecs& ecs, const SceneGraph& scene_graph, mat4x4 world_transform)
{
  Skin skin = scene_graph.skins[0];

  mat4x4 inverted_world_transform = {};
  mat4x4_invert(inverted_world_transform, world_transform);

  mat4x4* transforms = &ecs.node_transforms[base.node_transforms];
  mat4x4* skinning   = &ecs.joint_matrices[joint_matrices];

  for (int joint_id = 0; joint_id < skin.joints.count; ++joint_id)
  {
    mat4x4 transform = {};
    mat4x4_dup(transform, transforms[skin.joints[joint_id]]);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, inverted_world_transform, transform);

    mat4x4_mul(skinning[joint_id], tmp, skin.inverse_bind_matrices[joint_id]);
  }
}