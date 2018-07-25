#include "game.hh"

namespace {

void copy_quat(quat dst, const quat src)
{
  for (int i = 0; i < 4; ++i)
    dst[i] = src[i];
}

void copy_vec3(vec3 dst, const vec3 src)
{
  for (int i = 0; i < 3; ++i)
    dst[i] = src[i];
}

void depth_first_node_transform(mat4x4* transforms, Node* nodes, const int parent_node_idx, const int node_idx)
{
  mat4x4_mul(transforms[node_idx], transforms[parent_node_idx], transforms[node_idx]);
  for (int child_idx : nodes[node_idx].children)
    depth_first_node_transform(transforms, nodes, node_idx, child_idx);
}

} // namespace

void recalculate_node_transforms(const Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                 mat4x4 world_transform)
{
  const uint8_t*         node_parent_hierarchy = ecs.node_parent_hierarchies[entity.node_parent_hierarchy].hierarchy;
  const ArrayView<Node>& nodes                 = model.scene_graph.nodes;

  mat4x4 transforms[64] = {};

  for (int i = 0; i < model.scene_graph.nodes.count; ++i)
    mat4x4_identity(transforms[i]);

  for (int node_idx : model.scene_graph.scenes[0].nodes)
    mat4x4_dup(transforms[node_idx], world_transform);

  if (not model.scene_graph.skins.empty())
  {
    int skeleton_node_idx   = model.scene_graph.skins[0].skeleton;
    int skeleton_parent_idx = node_parent_hierarchy[skeleton_node_idx];
    mat4x4_dup(transforms[skeleton_parent_idx], world_transform);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Translations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 64; ++i)
  {
    mat4x4 translation_matrix = {};
    mat4x4_identity(translation_matrix);

    if (0 <= entity.animation_translation)
    {
      AnimationTranslation& comp = ecs.animation_translations[entity.animation_translation];
      if (comp.applicability & (1ULL << i))
      {
        float* t = comp.animations[i];
        mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
      }
      else if (nodes[i].has(Node::Property::Translation))
      {
        vec3 t = {};
        copy_vec3(t, nodes[i].translation);
        mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
      }
    }
    else if (nodes[i].has(Node::Property::Rotation))
    {
      vec3 t = {};
      copy_vec3(t, nodes[i].translation);
      mat4x4_translate(translation_matrix, t[0], t[1], t[2]);
    }

    mat4x4_mul(transforms[i], transforms[i], translation_matrix);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Rotations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 64; ++i)
  {
    mat4x4 rotation_matrix = {};
    mat4x4_identity(rotation_matrix);

    if (0 <= entity.animation_rotation)
    {
      AnimationRotation& comp = ecs.animation_rotations[entity.animation_rotation];
      if (comp.applicability & (1ULL << i))
      {
        mat4x4_from_quat(rotation_matrix, comp.rotations[i]);
      }
      else if (nodes[i].has(Node::Property::Rotation))
      {
        quat tmp = {};
        copy_quat(tmp, nodes[i].rotation);
        mat4x4_from_quat(rotation_matrix, tmp);
      }
    }
    else if (nodes[i].has(Node::Property::Rotation))
    {
      quat tmp = {};
      copy_quat(tmp, nodes[i].rotation);
      mat4x4_from_quat(rotation_matrix, tmp);
    }

    mat4x4_mul(transforms[i], transforms[i], rotation_matrix);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Scaling
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 64; ++i)
  {
    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);

    if (nodes[i].has(Node::Property::Scale))
    {
      vec3 s = {};
      copy_vec3(s, nodes[i].scale);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, s[0], s[1], s[2]);
    }

    mat4x4_mul(transforms[i], transforms[i], scale_matrix);
  }

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
  {
    if (node_idx == node_parent_hierarchy[node_idx])
      for (int child_idx : nodes[node_idx].children)
        depth_first_node_transform(transforms, nodes.data, node_idx, child_idx);
  }

  SDL_memcpy(ecs.node_transforms[entity.node_transforms].transforms, transforms, sizeof(transforms));
}

void recalculate_skinning_matrices(const Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                   mat4x4 world_transform)
{
  Skin skin = model.scene_graph.skins[0];

  mat4x4 inverted_world_transform = {};
  mat4x4_invert(inverted_world_transform, world_transform);

  mat4x4* transforms = ecs.node_transforms[entity.node_transforms].transforms;
  mat4x4* skinning   = ecs.joint_matrices[entity.joint_matrices].joints;

  for (int joint_id = 0; joint_id < skin.joints.count; ++joint_id)
  {
    mat4x4 transform = {};
    mat4x4_dup(transform, transforms[skin.joints[joint_id]]);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, inverted_world_transform, transform);

    mat4x4_mul(skinning[joint_id], tmp, skin.inverse_bind_matrices[joint_id]);
  }
}