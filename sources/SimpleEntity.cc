#include "SimpleEntity.hh"
#include <SDL2/SDL_assert.h>
#include <algorithm>

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
  dst |= (uint64_t(1) << static_cast<uint32_t>(node_idx));
}

void depth_first_node_transform(Mat4x4* transforms, Node* nodes, const int parent_node_idx, const int node_idx)
{
  transforms[node_idx] = transforms[parent_node_idx] * transforms[node_idx];
  for (int child_idx : nodes[node_idx].children)
    depth_first_node_transform(transforms, nodes, node_idx, child_idx);
}

} // namespace

void SimpleEntity::init(FreeListAllocator& allocator, const SceneGraph& model)
{
  const uint32_t nodes_count = static_cast<const uint32_t>(model.nodes.count);
  SDL_assert(nodes_count < 64);

  node_parent_hierarchy = allocator.allocate<uint8_t>(nodes_count);
  node_transforms       = allocator.allocate<Mat4x4>(nodes_count);

  for (int scene_node_idx : model.scenes[0].nodes)
    propagate_node_renderability_hierarchy(scene_node_idx, node_renderabilities, model.nodes);

  for (uint8_t i = 0; i < nodes_count; ++i)
    node_parent_hierarchy[i] = i;

  for (uint8_t node_idx = 0; node_idx < nodes_count; ++node_idx)
    for (int child_idx : model.nodes[node_idx].children)
      depth_first_node_parent_hierarchy(node_parent_hierarchy, model.nodes.data, node_idx,
                                        static_cast<uint8_t>(child_idx));

  if (model.skins.count)
    joint_matrices = allocator.allocate<Mat4x4>(static_cast<uint32_t>(model.skins[0].joints.count));
}

void SimpleEntity::recalculate_node_transforms(const SceneGraph& model, Mat4x4 world_transform)
{
  const ArrayView<Node>& nodes = model.nodes;

  Mat4x4 transforms[64];
  std::for_each(transforms, transforms + nodes.count, [](Mat4x4& it) { it.identity(); });

  for (int node_idx : model.scenes[0].nodes)
  {
    transforms[node_idx] = world_transform;
  }

  if (not model.skins.empty())
  {
    int skeleton_node_idx           = model.skins[0].skeleton;
    int skeleton_parent_idx         = node_parent_hierarchy[skeleton_node_idx];
    transforms[skeleton_parent_idx] = world_transform;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Translations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    Mat4x4 translation_matrix;
    translation_matrix.identity();

    if (flags & (Property::NodeTranslations | Property::NodeAnimTranslationApplicability))
    {
      if (node_anim_translation_applicability & (uint64_t(1) << static_cast<uint32_t>(i)))
      {
        translation_matrix.translate(node_translations[i]);
      }
      else if (nodes[i].flags & Node::Property::Translation)
      {
        translation_matrix.translate(nodes[i].translation);
      }
    }
    else if (nodes[i].flags & Node::Property::Translation)
    {
      translation_matrix.translate(nodes[i].translation);
    }

    transforms[i] = transforms[i] * translation_matrix;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Rotations
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    Mat4x4 rotation_matrix;
    rotation_matrix.identity();

    if (flags & (Property::NodeRotations | Property::NodeAnimRotationApplicability))
    {
      if (node_anim_rotation_applicability & (uint64_t(1) << static_cast<uint32_t>(i)))
      {
        rotation_matrix = Mat4x4(node_rotations[i]);
      }
      else if (nodes[i].flags & Node::Property::Rotation)
      {
        rotation_matrix = Mat4x4(nodes[i].rotation);
      }
    }
    else if (nodes[i].flags & Node::Property::Rotation)
    {
      rotation_matrix = Mat4x4(nodes[i].rotation);
    }

    transforms[i] = transforms[i] * rotation_matrix;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// Apply Scaling
  //////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < nodes.count; ++i)
  {
    if (nodes[i].flags & Node::Property::Scale)
    {
      Mat4x4 scale_matrix;
      scale_matrix.identity();
      scale_matrix.scale(nodes[i].scale);
      transforms[i] = transforms[i] * scale_matrix;
    }
  }

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
  {
    if (node_idx == node_parent_hierarchy[node_idx])
    {
      for (int child_idx : nodes[node_idx].children)
      {
        depth_first_node_transform(transforms, nodes.data, node_idx, child_idx);
      }
    }
  }

  std::copy(transforms, transforms + nodes.count, node_transforms);

  // recalculate_skinning_matrices
  if (joint_matrices)
  {
    Skin skin = model.skins[0];

    const Mat4x4 inverted_world_transform = world_transform.invert();
    for (int joint_id = 0; joint_id < skin.joints.count; ++joint_id)
    {
      joint_matrices[joint_id] =
          inverted_world_transform * node_transforms[skin.joints[joint_id]] * skin.inverse_bind_matrices[joint_id];
    }
  }
}
