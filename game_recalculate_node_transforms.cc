#include "game.hh"

namespace {

void initialize_matrices(mat4x4 matrices[], int n)
{
  for (int i = 0; i < n; ++i)
    mat4x4_identity(matrices[i]);
}

void copy_sparse(mat4x4 dst[], mat4x4 src, const int* indices, int n)
{
  for (int i = 0; i < n; ++i)
    mat4x4_dup(dst[indices[i]], src);
}

void copy_sparse_from_quat(mat4x4 dst[], quat* rotations, uint64_t bitmap)
{
  for (int i = 0; i < 64; ++i)
    if (bitmap & (1 << i))
      mat4x4_from_quat(dst[i], rotations[i]);
}

void copy_quat(quat dst, const quat src)
{
  for (int i = 0; i < 4; ++i)
    dst[i] = src[i];
}

void copy_rotations(quat dst[], const Node* nodes, int n)
{
  for (int i = 0; i < n; ++i)
    copy_quat(dst[i], nodes[i].rotation);
}

void copy_rotations(quat dst[], const ArrayView<Node>& nodes)
{
  copy_rotations(dst, nodes.data, nodes.count);
}

void copy_vec3(vec3 dst, const vec3 src)
{
  for (int i = 0; i < 3; ++i)
    dst[i] = src[i];
}

void copy_translations(vec3 dst[], const Node* nodes, int n)
{
  for (int i = 0; i < n; ++i)
    copy_vec3(dst[i], nodes[i].translation);
}

void copy_translations(vec3 dst[], const ArrayView<Node>& nodes)
{
  copy_translations(dst, nodes.data, nodes.count);
}

void copy_scales(vec3 dst[], const Node* nodes, int n)
{
  for (int i = 0; i < n; ++i)
    copy_vec3(dst[i], nodes[i].scale);
}

void copy_scales(vec3 dst[], const ArrayView<Node>& nodes)
{
  copy_scales(dst, nodes.data, nodes.count);
}

uint64_t gather_properties(const Node* nodes, int n, Node::Property property)
{
  uint64_t result = 0;
  for (int i = 0; i < n; ++i)
    if (nodes[n].has(property))
      result |= (1 << i);
  return result;
}

uint64_t gather_rotation_properties(const ArrayView<Node>& nodes)
{
  return gather_properties(nodes.data, nodes.count, Node::Property::Rotation);
}

uint64_t gather_translation_properties(const ArrayView<Node>& nodes)
{
  return gather_properties(nodes.data, nodes.count, Node::Property::Translation);
}

uint64_t gather_scale_properties(const ArrayView<Node>& nodes)
{
  return gather_properties(nodes.data, nodes.count, Node::Property::Scale);
}

void translate(mat4x4 dst, const vec3 src)
{
  for (int i = 0; i < 3; ++i)
    dst[3][i] = src[i];
}

void translate_sparse(mat4x4 dst[], vec3* src, const uint64_t bitmap)
{
  for (int i = 0; i < 64; ++i)
    if (bitmap & (1 << i))
      translate(dst[i], src[i]);
}

void scale(mat4x4 dst, const vec3 src)
{
  mat4x4_scale_aniso(dst, dst, src[0], src[1], src[2]);
}

void scale_sparse(mat4x4 dst[], vec3* src, const uint64_t bitmap)
{
  for (int i = 0; i < 64; ++i)
    if (bitmap & (1 << i))
      scale(dst[i], src[i]);
}

void multiply_matrices_into_rhs(mat4x4 lhs[], mat4x4 rhs[], int n)
{
  for (int i = 0; i < n; ++i)
    mat4x4_mul(rhs[i], lhs[i], rhs[i]);
}

void multiply_matrices_into_lhs(mat4x4 lhs[], mat4x4 rhs[], int n)
{
  for (int i = 0; i < n; ++i)
    mat4x4_mul(lhs[i], lhs[i], rhs[i]);
}

uint64_t clear_bits(uint64_t dst, uint64_t rhs)
{
  return dst ^ (dst & rhs);
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

  mat4x4 local_transforms[64] = {};
  initialize_matrices(local_transforms, nodes.count);
  copy_sparse(local_transforms, world_transform, = model.scene_graph.scenes[0].nodes.data, nodes.count);

  if (not model.scene_graph.skins.empty())
  {
    int skeleton_node_idx   = model.scene_graph.skins[0].skeleton;
    int skeleton_parent_idx = node_parent_hierarchy[skeleton_node_idx];
    mat4x4_dup(local_transforms[skeleton_parent_idx], world_transform);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Rotations
  //////////////////////////////////////////////////////////////////////////////
  {
    uint64_t rotations_from_animations = 0;
    uint64_t rotations_from_properties = 0;

    if (0 <= entity.animation_rotation)
    {
      AnimationRotation& comp   = ecs.animation_rotations[entity.animation_rotation];
      rotations_from_animations = comp.applicability;
      copy_sparse_from_quat(local_transforms, comp.rotations, rotations_from_animations);
    }

    rotations_from_properties = clear_bits(gather_rotation_properties(nodes), rotations_from_animations);

    quat rotations[64] = {};
    copy_rotations(rotations, nodes);
    copy_sparse_from_quat(local_transforms, rotations, rotations_from_properties);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Translations
  //////////////////////////////////////////////////////////////////////////////
  {
    uint64_t translations_from_animations = 0;
    uint64_t translations_from_properties = 0;

    mat4x4 translations[64] = {};
    initialize_matrices(translations, SDL_arraysize(translations));

    if (0 <= entity.animation_translation)
    {
      AnimationTranslation& comp   = ecs.animation_translations[entity.animation_translation];
      translations_from_animations = comp.applicability;
      translate_sparse(translations, comp.animations, comp.applicability);
    }

    translations_from_properties = clear_bits(gather_translation_properties(nodes), translations_from_animations);

    vec3 property_translations[64] = {};
    copy_translations(property_translations, nodes);
    translate_sparse(translations, property_translations, translations_from_properties);
    multiply_matrices_into_rhs(translations, local_transforms, SDL_arraysize(translations));
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Scaling
  //////////////////////////////////////////////////////////////////////////////
  {
    mat4x4 scales[64] = {};
    initialize_matrices(scales, SDL_arraysize(scales));

    vec3 property_scales[64] = {};
    copy_scales(property_scales, nodes);
    scale_sparse(scales, property_scales, gather_scale_properties(nodes));
    multiply_matrices_into_lhs(local_transforms, scales, SDL_arraysize(scales));
  }

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
  {
    if (node_idx == node_parent_hierarchy[node_idx])
      for (int child_idx : nodes[node_idx].children)
        depth_first_node_transform(local_transforms, nodes.data, node_idx, child_idx);
  }

  SDL_memcpy(ecs.node_transforms[entity.node_transforms].transforms, local_transforms, sizeof(local_transforms));
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
    mat4x4 tmp = {};
    mat4x4_mul(tmp, inverted_world_transform, transforms[skin.joints[joint_id]]);
    mat4x4_mul(skinning[joint_id], tmp, skin.inverse_bind_matrices[joint_id]);
  }
}