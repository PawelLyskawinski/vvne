#include "update_jobs.hh"

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (static_cast<float>(M_PI) * deg) / 180.0f;
}

constexpr float to_deg(float rad) noexcept
{
  return (180.0f * rad) / static_cast<float>(M_PI);
}

int find_first_higher(const float times[], float current)
{
  int iter = 0;
  while (current > times[iter])
    iter += 1;
  return iter;
}

template <int DIM> void lerp(const float a[], const float b[], float result[], float t)
{
  for (int i = 0; i < DIM; ++i)
  {
    float distance = b[i] - a[i];
    float progress = distance * t;
    result[i]      = a[i] + progress;
  }
}

// https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#appendix-c-spline-interpolation
void hermite_cubic_spline_interpolation(const float a_in[], const float b_in[], float result[], int dim, float t,
                                        float total_duration)
{
  const float* a_spline_vertex = &a_in[dim];
  const float* a_out_tangent   = &a_in[2 * dim];

  const float* b_in_tangent    = &b_in[0];
  const float* b_spline_vertex = &b_in[dim];

  for (int i = 0; i < dim; ++i)
  {
    float P[2] = {a_spline_vertex[i], b_spline_vertex[i]};
    float M[2] = {a_out_tangent[i], b_in_tangent[i]};

    for (float& m : M)
      m *= total_duration;

    float a   = (2.0f * P[0]) + M[0] + (-2.0f * P[1]) + M[1];
    float b   = (-3.0f * P[0]) - (2.0f * M[0]) + (3.0f * P[1]) - M[1];
    float c   = M[0];
    float d   = P[0];
    result[i] = (a * t * t * t) + (b * t * t) + (c * t) + (d);
  }
}

void animate_entity(SimpleEntity& entity, Ecs& ecs, SceneGraph& scene_graph, float current_time_sec)
{
  if (0 == (entity.flags & SimpleEntity::AnimationStartTime))
    return;

  const float      animation_start_time = entity.animation_start_time;
  const Animation& animation            = scene_graph.animations.data[0];
  const float      animation_time       = current_time_sec - animation_start_time;

  bool is_animation_still_ongoing = false;
  for (const AnimationChannel& channel : animation.channels)
  {
    const AnimationSampler& sampler = animation.samplers[channel.sampler_idx];
    if (sampler.time_frame[1] > animation_time)
    {
      is_animation_still_ongoing = true;
      break;
    }
  }

  if (not is_animation_still_ongoing)
  {
    const uint64_t clear_mask = SimpleEntity::NodeAnimRotationApplicability |
                                SimpleEntity::NodeAnimTranslationApplicability | SimpleEntity::AnimationStartTime;

    entity.flags &= ~clear_mask;
    entity.animation_start_time                = 0.0f;
    entity.node_anim_rotation_applicability    = 0;
    entity.node_anim_translation_applicability = 0;
    return;
  }

  for (const AnimationChannel& channel : animation.channels)
  {
    const AnimationSampler& sampler = animation.samplers[channel.sampler_idx];
    if ((sampler.time_frame[1] > animation_time) and (sampler.time_frame[0] < animation_time))
    {
      int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
      int   keyframe_lower         = keyframe_upper - 1;
      float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
      float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

      if (AnimationChannel::Path::Rotation == channel.target_path)
      {
        if (0 == (entity.flags & SimpleEntity::NodeRotations))
        {
          entity.node_rotations = ecs.node_anim_rotations_stack.increment(scene_graph.nodes.count);
          entity.flags |= SimpleEntity::NodeRotations;
        }

        quat* animation_rotation = &ecs.node_anim_rotations[entity.node_rotations];

        if (0 == (entity.flags & SimpleEntity::NodeAnimRotationApplicability))
        {
          SDL_memset(animation_rotation, 0, scene_graph.nodes.count * sizeof(quat));
          entity.flags |= SimpleEntity::NodeAnimRotationApplicability;
        }

        entity.node_anim_rotation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[4 * keyframe_lower];
          float* b = &sampler.values[4 * keyframe_upper];
          float* c = animation_rotation[channel.target_node_idx];
          lerp<4>(a, b, c, keyframe_uniform_time);
          vec4_norm(c, c);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = animation_rotation[channel.target_node_idx];
          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
          vec4_norm(c, c);
        }
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        if (0 == (entity.flags & SimpleEntity::NodeTranslations))
        {
          entity.node_translations = ecs.node_anim_translations_stack.increment(scene_graph.nodes.count);
          entity.flags |= SimpleEntity::NodeTranslations;
        }

        vec3* animation_translation = &ecs.node_anim_translations[entity.node_translations];

        if (0 == (entity.flags & SimpleEntity::NodeAnimTranslationApplicability))
        {
          SDL_memset(animation_translation, 0, scene_graph.nodes.count * sizeof(vec3));
          entity.flags |= SimpleEntity::NodeAnimTranslationApplicability;
        }

        entity.node_anim_translation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[3 * keyframe_lower];
          float* b = &sampler.values[3 * keyframe_upper];
          float* c = animation_translation[channel.target_node_idx];
          lerp<3>(a, b, c, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = animation_translation[channel.target_node_idx];
          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
        }
      }
    }
  }
}

void multiply(quat result, quat lhs, quat rhs)
{
  quat_mul(result, lhs, rhs);
}

template <typename... Args> void multiply(quat result, quat lhs, quat rhs, Args... args)
{
  quat tmp = {};
  quat_mul(tmp, lhs, rhs);
  multiply(result, tmp, args...);
}

void multiply(mat4x4 result, mat4x4 lhs, mat4x4 rhs)
{
  mat4x4_mul(result, lhs, rhs);
}

template <typename... Args> void multiply(mat4x4 result, mat4x4 lhs, mat4x4 rhs, Args... args)
{
  mat4x4 tmp = {};
  mat4x4_mul(tmp, lhs, rhs);
  multiply(result, tmp, args...);
}

} // namespace

namespace update {

void helmet_job(ThreadJobData tjd)
{
  quat orientation = {};
  vec3 x_axis      = {1.0, 0.0, 0.0};
  quat_rotate(orientation, to_rad(180.0), x_axis);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.vr_level_goal[0], 3.0f, tjd.game.vr_level_goal[1]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, 1.6f, 1.6f, 1.6f);

  mat4x4 world_transform = {};
  multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

  tjd.game.helmet_entity.recalculate_node_transforms(tjd.game.ecs, tjd.game.helmet, world_transform);
}

void robot_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {0.0, 1.0, 0.0};
  vec3 z_axis = {0.0, 0.0, 1.0};

  quat standing_pose = {};
  quat_rotate(standing_pose, to_rad(180.0), x_axis);

  quat rotate_back = {};
  quat_rotate(rotate_back, tjd.game.player_position[0] < tjd.game.camera_position[0] ? to_rad(180.0f) : to_rad(0.0f),
              y_axis);

  float x_delta = tjd.game.player_position[0] - tjd.game.camera_position[0];
  float z_delta = tjd.game.player_position[2] - tjd.game.camera_position[2];

  quat camera = {};
  quat_rotate(camera, static_cast<float>(SDL_atan(z_delta / x_delta)), y_axis);

  //////////////////////////////////////////////////////////////////////////////
  // Adaptive model tilt when moving
  //////////////////////////////////////////////////////////////////////////////

  vec2  velocity_vector           = {tjd.game.player_velocity[0], tjd.game.player_velocity[2]};
  float velocity_length           = vec2_len(velocity_vector);
  float velocity_angle            = SDL_atan2f(velocity_vector[0], velocity_vector[1]);
  float relative_velocity_angle   = tjd.game.camera_angle - velocity_angle;
  vec2  corrected_velocity_vector = {velocity_length * SDL_cosf(relative_velocity_angle),
                                    velocity_length * SDL_sinf(relative_velocity_angle)};

  quat movement_tilt[2] = {};
  quat_rotate(movement_tilt[0], 8.0f * corrected_velocity_vector[0], x_axis);
  quat_rotate(movement_tilt[1], -8.0f * corrected_velocity_vector[1], z_axis);

  quat orientation = {};
  multiply(orientation, standing_pose, rotate_back, camera, movement_tilt[0], movement_tilt[1]);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.player_position[0], tjd.game.player_position[1] - 1.0f,
                   tjd.game.player_position[2]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

  mat4x4 world_transform = {};
  multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

  tjd.game.robot_entity.recalculate_node_transforms(tjd.game.ecs, tjd.game.robot, world_transform);
}

void monster_job(ThreadJobData tjd)
{
  mat4x4 translation_matrix = {};
  mat4x4_translate(
      translation_matrix,
      // tjd.game.light_source_position[0], tjd.game.light_source_position[1], tjd.game.light_source_position[2]);
      -2.0f, 3.5f, 0.5f);

  quat orientation = {};
  vec3 x_axis      = {1.0, 0.0, 0.0};
  quat_rotate(orientation, to_rad(45.0f), x_axis);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  float factor = 0.025f;
  mat4x4_scale_aniso(scale_matrix, scale_matrix, factor, factor, factor);

  mat4x4 world_transform = {};
  multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

  animate_entity(tjd.game.monster_entity.base, tjd.game.ecs, tjd.game.monster, tjd.game.current_time_sec);
  tjd.game.monster_entity.base.recalculate_node_transforms(tjd.game.ecs, tjd.game.monster, world_transform);
  tjd.game.monster_entity.recalculate_skinning_matrices(tjd.game.ecs, tjd.game.monster, world_transform);
}

void rigged_simple_job(ThreadJobData tjd)
{
  quat orientation = {};
  vec3 x_axis      = {1.0, 0.0, 0.0};
  quat_rotate(orientation, to_rad(45.0f), x_axis);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.rigged_position[0], tjd.game.rigged_position[1],
                   tjd.game.rigged_position[2]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

  mat4x4 world_transform = {};
  multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

  animate_entity(tjd.game.rigged_simple_entity.base, tjd.game.ecs, tjd.game.riggedSimple, tjd.game.current_time_sec);
  tjd.game.rigged_simple_entity.base.recalculate_node_transforms(tjd.game.ecs, tjd.game.riggedSimple, world_transform);
  tjd.game.rigged_simple_entity.recalculate_skinning_matrices(tjd.game.ecs, tjd.game.riggedSimple, world_transform);
}

void moving_lights_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {1.0, 0.0, 0.0};
  vec3 z_axis = {1.0, 0.0, 0.0};

  for (int i = 0; i < tjd.game.pbr_light_sources_cache.count; ++i)
  {
    quat z = {};
    quat_rotate(z, to_rad(100.0f * tjd.game.current_time_sec), z_axis);

    quat y = {};
    quat_rotate(y, to_rad(280.0f * tjd.game.current_time_sec), y_axis);

    quat x = {};
    quat_rotate(x, to_rad(60.0f * tjd.game.current_time_sec), x_axis);

    quat orientation = {};
    multiply(orientation, z, y, x);

    float* position = tjd.game.pbr_light_sources_cache.positions[i];

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, position[0], position[1], position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.05f, 0.05f, 0.05f);

    mat4x4 world_transform = {};
    multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

    tjd.game.box_entities[i].recalculate_node_transforms(tjd.game.ecs, tjd.game.box, world_transform);
  }
}

void matrioshka_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {1.0, 0.0, 0.0};
  vec3 z_axis = {1.0, 0.0, 0.0};

  quat z = {};
  quat_rotate(z, to_rad(90.0f * tjd.game.current_time_sec / 90.0f), z_axis);

  quat y = {};
  quat_rotate(y, to_rad(140.0f * tjd.game.current_time_sec / 30.0f), y_axis);

  quat x = {};
  quat_rotate(x, to_rad(90.0f * tjd.game.current_time_sec / 20.0f), x_axis);

  quat orientation = {};
  multiply(orientation, z, y, x);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.robot_position[0], tjd.game.robot_position[1],
                   tjd.game.robot_position[2]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 world_transform = {};
  mat4x4_mul(world_transform, translation_matrix, rotation_matrix);

  animate_entity(tjd.game.matrioshka_entity, tjd.game.ecs, tjd.game.animatedBox, tjd.game.current_time_sec);
  tjd.game.matrioshka_entity.recalculate_node_transforms(tjd.game.ecs, tjd.game.animatedBox, world_transform);
}

void orientation_axis_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {0.0, 1.0, 0.0};

  for (int i = 0; i < 3; ++i)
  {
    vec3 translation = {};
    SDL_memcpy(translation, tjd.game.player_position, sizeof(vec3));

    const vec3  scale              = {1.0f, 1.0f, 0.5f};
    const float translation_offset = 2.0f;

    quat orientation = {};
    switch (i)
    {
    default:
    case 0:
      quat_rotate(orientation, -to_rad(90.0f), y_axis);
      translation[0] += translation_offset;
      break;
    case 1:
      quat_rotate(orientation, -to_rad(90.0f), x_axis);
      translation[1] -= translation_offset;
      break;
    case 2:
      quat_rotate(orientation, to_rad(180.0f), x_axis);
      translation[2] += translation_offset;
      break;
    }

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, translation[0], translation[1], translation[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, scale[0], scale[1], scale[2]);

    mat4x4 world_transform = {};
    multiply(world_transform, translation_matrix, rotation_matrix, scale_matrix);

    tjd.game.axis_arrow_entities[i].recalculate_node_transforms(tjd.game.ecs, tjd.game.lil_arrow, world_transform);
  }
}

} // namespace update
