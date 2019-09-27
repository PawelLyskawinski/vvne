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

//
// https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#appendix-c-spline-interpolation
//
void hermite_cubic_spline_interpolation(const float a_in[], const float b_in[], float result[], int dim, float t,
                                        float total_duration)
{
  const float* a_spline_vertex = &a_in[dim];
  const float* a_out_tangent   = &a_in[2 * dim];

  const float* b_in_tangent    = &b_in[0];
  const float* b_spline_vertex = &b_in[dim];

  for (int i = 0; i < dim; ++i)
  {
    const Vec2  P = Vec2(a_spline_vertex[i], b_spline_vertex[i]);
    const Vec2  M = Vec2(a_out_tangent[i], b_in_tangent[i]).scale(total_duration);
    const float a = (2.0f * P.x) + M.x + (-2.0f * P.y) + M.y;
    const float b = (-3.0f * P.x) - (2.0f * M.x) + (3.0f * P.y) - M.y;

    result[i] = (a * t * t * t) + (b * t * t) + (M.x * t) + P.x;
  }
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

void SimpleEntity::recalculate_node_transforms(const SceneGraph& model, const Mat4x4& world_transform)
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

void SimpleEntity::animate(FreeListAllocator& allocator, const SceneGraph& scene_graph, float current_time_sec)
{
  //
  // Animation is considered running ONLY when entity has a proper flag setup.
  // In case it doesn't have it, we can skip executing this function.
  //

  if (0 == (flags & SimpleEntity::AnimationStartTime))
  {
    return;
  }

  const Animation& animation      = scene_graph.animations.data[0];
  const float      animation_time = current_time_sec - animation_start_time;

  if (std::none_of(
          animation.samplers.begin(), animation.samplers.end(),
          [animation_time](const AnimationSampler& sampler) { return sampler.time_frame[1] > animation_time; }))
  {
    const uint64_t clear_mask = SimpleEntity::NodeAnimRotationApplicability |
                                SimpleEntity::NodeAnimTranslationApplicability | SimpleEntity::AnimationStartTime;

    flags &= ~clear_mask;
    animation_start_time                = 0.0f;
    node_anim_rotation_applicability    = 0;
    node_anim_translation_applicability = 0;
    return;
  }

  for (const AnimationChannel& channel : animation.channels)
  {
    const AnimationSampler& sampler = animation.samplers[channel.sampler_idx];
    if ((sampler.time_frame[1] > animation_time) and (sampler.time_frame[0] < animation_time))
    {
      int keyframe_upper = std::distance(
          sampler.times, std::lower_bound(sampler.times, sampler.times + sampler.keyframes_count, animation_time));

      int keyframe_lower = keyframe_upper - 1;

      float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
      float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

      if (AnimationChannel::Path::Rotation == channel.target_path)
      {
        if (0 == (flags & SimpleEntity::NodeRotations))
        {
          node_rotations = allocator.allocate<Quaternion>(static_cast<uint32_t>(scene_graph.nodes.count));
          flags |= SimpleEntity::NodeRotations;
        }

        if (0 == (flags & SimpleEntity::NodeAnimRotationApplicability))
        {
          std::fill(node_rotations, node_rotations + scene_graph.nodes.count, Quaternion());
          flags |= SimpleEntity::NodeAnimRotationApplicability;
        }

        node_anim_rotation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          const Vec4* samples = reinterpret_cast<Vec4*>(sampler.values);
          const Vec4& a       = samples[keyframe_lower];
          const Vec4& b       = samples[keyframe_upper];

          Vec4* c = reinterpret_cast<Vec4*>(&node_rotations[channel.target_node_idx].data.x);

          *c = a.lerp(b, keyframe_uniform_time).normalize();
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = &node_rotations[channel.target_node_idx].data.x;

          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);

          Vec4* c_as_vec4 = reinterpret_cast<Vec4*>(c);
          *c_as_vec4      = c_as_vec4->normalize();
        }
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        if (0 == (flags & SimpleEntity::NodeTranslations))
        {
          node_translations = allocator.allocate<Vec3>(static_cast<uint32_t>(scene_graph.nodes.count));
          flags |= SimpleEntity::NodeTranslations;
        }

        if (0 == (flags & SimpleEntity::NodeAnimTranslationApplicability))
        {
          std::fill(node_translations, node_translations + scene_graph.nodes.count, Vec3());
          flags |= SimpleEntity::NodeAnimTranslationApplicability;
        }

        node_anim_translation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          Vec3* a = reinterpret_cast<Vec3*>(&sampler.values[3 * keyframe_lower]);
          Vec3* b = reinterpret_cast<Vec3*>(&sampler.values[3 * keyframe_upper]);
          Vec3* c = reinterpret_cast<Vec3*>(&node_translations[channel.target_node_idx].x);

          *c = a->lerp(*b, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = &node_translations[channel.target_node_idx].x;

          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
        }
      }
    }
  }
}
