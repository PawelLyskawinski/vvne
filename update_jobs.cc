#include "update_jobs.hh"

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

constexpr float to_deg(float rad) noexcept
{
  return (180.0f * rad) / float(M_PI);
}

int find_first_higher(const float times[], float current)
{
  int iter = 0;
  while (current > times[iter])
    iter += 1;
  return iter;
}

void lerp(const float a[], const float b[], float result[], int dim, float t)
{
  for (int i = 0; i < dim; ++i)
  {
    float difference = b[i] - a[i];
    float progressed = difference * t;
    result[i]        = a[i] + progressed;
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

void animate_entity(Entity& entity, EntityComponentSystem& ecs, SceneGraph& scene_graph, float current_time_sec)
{
  if (-1 == entity.animation_start_time)
    return;

  const float      animation_start_time = ecs.animation_start_times[entity.animation_start_time];
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
    ecs.animation_start_times_usage.free(entity.animation_start_time);
    ecs.animation_rotations_usage.free(entity.animation_rotation);
    ecs.animation_translations_usage.free(entity.animation_translation);

    entity.animation_start_time  = -1;
    entity.animation_rotation    = -1;
    entity.animation_translation = -1;

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
        AnimationRotation* rotation_component = nullptr;

        if (-1 == entity.animation_rotation)
        {
          entity.animation_rotation = ecs.animation_rotations_usage.allocate();
          rotation_component        = &ecs.animation_rotations[entity.animation_rotation];
          SDL_memset(rotation_component, 0, sizeof(AnimationRotation));
        }
        else
        {
          rotation_component = &ecs.animation_rotations[entity.animation_rotation];
        }

        rotation_component->applicability |= (1ULL << channel.target_node_idx);

        float* animation_rotation = rotation_component->rotations[channel.target_node_idx];

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[4 * keyframe_lower];
          float* b = &sampler.values[4 * keyframe_upper];
          float* c = animation_rotation;
          lerp(a, b, c, 4, keyframe_uniform_time);
          vec4_norm(c, c);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = animation_rotation;
          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
          vec4_norm(c, c);
        }
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        AnimationTranslation* translation_component = nullptr;

        if (-1 == entity.animation_translation)
        {
          entity.animation_translation = ecs.animation_translations_usage.allocate();
          translation_component        = &ecs.animation_translations[entity.animation_translation];
          SDL_memset(translation_component, 0, sizeof(AnimationTranslation));
        }
        else
        {
          translation_component = &ecs.animation_translations[entity.animation_translation];
        }

        translation_component->applicability |= (1ULL << channel.target_node_idx);

        float* animation_translation = translation_component->animations[channel.target_node_idx];

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[3 * keyframe_lower];
          float* b = &sampler.values[3 * keyframe_upper];
          float* c = animation_translation;
          lerp(a, b, c, 3, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = animation_translation;
          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
        }
      }
    }
  }
}

} // namespace

// game_recalculate_node_transforms.cc
void recalculate_node_transforms(Entity entity, EntityComponentSystem& ecs, const SceneGraph& scene_graph,
                                 mat4x4 world_transform);
void recalculate_skinning_matrices(Entity entity, EntityComponentSystem& ecs, const SceneGraph& scene_graph,
                                   mat4x4 world_transform);

namespace update {

void helmet_job(ThreadJobData tjd)
{
  mat4x4 world_transform = {};

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

  mat4x4 tmp = {};
  mat4x4_mul(tmp, translation_matrix, rotation_matrix);
  mat4x4_mul(world_transform, tmp, scale_matrix);

  recalculate_node_transforms(tjd.game.helmet_entity, tjd.game.ecs, tjd.game.helmet, world_transform);
}

void robot_job(ThreadJobData tjd)
{
  quat standing_pose = {};
  vec3 x_axis        = {1.0, 0.0, 0.0};
  quat_rotate(standing_pose, to_rad(180.0), x_axis);

  quat rotate_back = {};
  vec3 y_axis      = {0.0, 1.0, 0.0};
  quat_rotate(rotate_back, tjd.game.player_position[0] < tjd.game.camera_position[0] ? to_rad(180.0f) : to_rad(0.0f),
              y_axis);

  float x_delta = tjd.game.player_position[0] - tjd.game.camera_position[0];
  float z_delta = tjd.game.player_position[2] - tjd.game.camera_position[2];

  quat camera = {};
  quat_rotate(camera, static_cast<float>(SDL_atan(z_delta / x_delta)), y_axis);

  quat orientation = {};

  {
    quat tmp = {};
    quat_mul(tmp, standing_pose, rotate_back);
    quat_mul(orientation, tmp, camera);
  }

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.player_position[0], tjd.game.player_position[1] - 1.0f,
                   tjd.game.player_position[2]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

  mat4x4 world_transform = {};

  {
    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  recalculate_node_transforms(tjd.game.robot_entity, tjd.game.ecs, tjd.game.robot, world_transform);
}

void monster_job(ThreadJobData tjd)
{
  quat orientation = {};
  vec3 x_axis      = {1.0, 0.0, 0.0};
  quat_rotate(orientation, to_rad(45.0f), x_axis);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, -2.0f, 5.5f, 0.5f);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  float factor = 0.025f;
  mat4x4_scale_aniso(scale_matrix, scale_matrix, factor, factor, factor);

  mat4x4 world_transform = {};

  {
    mat4x4 tmp = {};
    mat4x4_mul(tmp, rotation_matrix, translation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  animate_entity(tjd.game.monster_entity, tjd.game.ecs, tjd.game.monster, tjd.game.current_time_sec);
  recalculate_node_transforms(tjd.game.monster_entity, tjd.game.ecs, tjd.game.monster, world_transform);
  recalculate_skinning_matrices(tjd.game.monster_entity, tjd.game.ecs, tjd.game.monster, world_transform);
}

void rigged_simple_job(ThreadJobData tjd)
{
  mat4x4 world_transform = {};

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

  mat4x4 tmp = {};
  mat4x4_mul(tmp, translation_matrix, rotation_matrix);
  mat4x4_mul(world_transform, tmp, scale_matrix);

  animate_entity(tjd.game.rigged_simple_entity, tjd.game.ecs, tjd.game.riggedSimple, tjd.game.current_time_sec);
  recalculate_node_transforms(tjd.game.rigged_simple_entity, tjd.game.ecs, tjd.game.riggedSimple, world_transform);
  recalculate_skinning_matrices(tjd.game.rigged_simple_entity, tjd.game.ecs, tjd.game.riggedSimple, world_transform);
}

void moving_lights_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {1.0, 0.0, 0.0};
  vec3 z_axis = {1.0, 0.0, 0.0};

  for (int i = 0; i < tjd.game.pbr_light_sources_cache.count; ++i)
  {
    quat orientation = {};

    {
      quat z = {};
      quat_rotate(z, to_rad(100.0f * tjd.game.current_time_sec), z_axis);

      quat y = {};
      quat_rotate(y, to_rad(280.0f * tjd.game.current_time_sec), y_axis);

      quat x = {};
      quat_rotate(x, to_rad(60.0f * tjd.game.current_time_sec), x_axis);

      quat tmp = {};
      quat_mul(tmp, z, y);
      quat_mul(orientation, tmp, x);
    }

    float* position = tjd.game.pbr_light_sources_cache.positions[i];

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, position[0], position[1], position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.05f, 0.05f, 0.05f);

    mat4x4 world_transform = {};

    {
      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);
      mat4x4_mul(world_transform, tmp, scale_matrix);
    }

    recalculate_node_transforms(tjd.game.box_entities[i], tjd.game.ecs, tjd.game.box, world_transform);
  }
}

void matrioshka_job(ThreadJobData tjd)
{
  vec3 x_axis = {1.0, 0.0, 0.0};
  vec3 y_axis = {1.0, 0.0, 0.0};
  vec3 z_axis = {1.0, 0.0, 0.0};

  quat orientation = {};

  quat z = {};
  quat_rotate(z, to_rad(90.0f * tjd.game.current_time_sec / 90.0f), z_axis);

  quat y = {};
  quat_rotate(y, to_rad(140.0f * tjd.game.current_time_sec / 30.0f), y_axis);

  quat x = {};
  quat_rotate(x, to_rad(90.0f * tjd.game.current_time_sec / 20.0f), x_axis);

  {
    quat tmp = {};
    quat_mul(tmp, z, y);
    quat_mul(orientation, tmp, x);
  }

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.robot_position[0], tjd.game.robot_position[1],
                   tjd.game.robot_position[2]);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation);

  mat4x4 world_transform = {};
  mat4x4_mul(world_transform, translation_matrix, rotation_matrix);

  animate_entity(tjd.game.matrioshka_entity, tjd.game.ecs, tjd.game.animatedBox, tjd.game.current_time_sec);
  recalculate_node_transforms(tjd.game.matrioshka_entity, tjd.game.ecs, tjd.game.animatedBox, world_transform);
}

} // namespace update
