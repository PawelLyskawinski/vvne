#include "update_jobs.hh"
#include <SDL2/SDL_log.h>

#include <algorithm>

namespace {

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

void animate_entity(SimpleEntity& entity, FreeListAllocator& allocator, SceneGraph& scene_graph, float current_time_sec)
{
  if (0 == (entity.flags & SimpleEntity::AnimationStartTime))
  {
    return;
  }

  const float      animation_start_time = entity.animation_start_time;
  const Animation& animation            = scene_graph.animations.data[0];
  const float      animation_time       = current_time_sec - animation_start_time;

  if (std::none_of(
          animation.samplers.begin(), animation.samplers.end(),
          [animation_time](const AnimationSampler& sampler) { return sampler.time_frame[1] > animation_time; }))
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
      int keyframe_upper = std::distance(
          sampler.times, std::lower_bound(sampler.times, sampler.times + sampler.keyframes_count, animation_time));

      int keyframe_lower = keyframe_upper - 1;

      float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
      float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

      if (AnimationChannel::Path::Rotation == channel.target_path)
      {
        if (0 == (entity.flags & SimpleEntity::NodeRotations))
        {
          entity.node_rotations = allocator.allocate<Quaternion>(static_cast<uint32_t>(scene_graph.nodes.count));
          entity.flags |= SimpleEntity::NodeRotations;
        }

        if (0 == (entity.flags & SimpleEntity::NodeAnimRotationApplicability))
        {
          std::fill(entity.node_rotations, entity.node_rotations + scene_graph.nodes.count, Quaternion());
          entity.flags |= SimpleEntity::NodeAnimRotationApplicability;
        }

        entity.node_anim_rotation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          const Vec4* samples = reinterpret_cast<Vec4*>(sampler.values);
          const Vec4& a       = samples[keyframe_lower];
          const Vec4& b       = samples[keyframe_upper];

          Vec4* c = reinterpret_cast<Vec4*>(&entity.node_rotations[channel.target_node_idx].data.x);

          *c = a.lerp(b, keyframe_uniform_time).normalize();
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = &entity.node_rotations[channel.target_node_idx].data.x;

          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);

          Vec4* c_as_vec4 = reinterpret_cast<Vec4*>(c);
          *c_as_vec4      = c_as_vec4->normalize();
        }
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        if (0 == (entity.flags & SimpleEntity::NodeTranslations))
        {
          entity.node_translations = allocator.allocate<Vec3>(static_cast<uint32_t>(scene_graph.nodes.count));
          entity.flags |= SimpleEntity::NodeTranslations;
        }

        if (0 == (entity.flags & SimpleEntity::NodeAnimTranslationApplicability))
        {
          std::fill(entity.node_translations, entity.node_translations + scene_graph.nodes.count, Vec3());
          entity.flags |= SimpleEntity::NodeAnimTranslationApplicability;
        }

        entity.node_anim_translation_applicability |= (uint64_t(1) << channel.target_node_idx);

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          Vec3* a = reinterpret_cast<Vec3*>(&sampler.values[3 * keyframe_lower]);
          Vec3* b = reinterpret_cast<Vec3*>(&sampler.values[3 * keyframe_upper]);
          Vec3* c = reinterpret_cast<Vec3*>(&entity.node_translations[channel.target_node_idx].x);

          *c = a->lerp(*b, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = &entity.node_translations[channel.target_node_idx].x;

          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
        }
      }
    }
  }
}

} // namespace

namespace update {

void helmet_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(0.0f, 6.0f, 0.0f)) *
                                 Mat4x4(Quaternion(to_rad(180.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(1.6f));

  ctx->game->helmet_entity.recalculate_node_transforms(ctx->game->materials.helmet, world_transform);
}

void robot_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  const Player& player                  = ctx->game->player;
  const float   x_delta                 = player.position.x - player.camera_position.x;
  const float   z_delta                 = player.position.z - player.camera_position.z;
  const Vec2    velocity_vector         = player.velocity.xz();
  const float   velocity_angle          = SDL_atan2f(velocity_vector.x, velocity_vector.y);
  const float   relative_velocity_angle = player.camera_angle - velocity_angle;
  const Vec2    corrected_velocity_vector =
      Vec2(SDL_cosf(relative_velocity_angle), SDL_sinf(relative_velocity_angle)).scale(velocity_vector.len());

  const Quaternion orientation =
      Quaternion(to_rad(180.0), Vec3(1.0f, 0.0f, 0.0f)) *
      Quaternion(ctx->game->player.position.x < ctx->game->player.camera_position.x ? to_rad(180.0f) : to_rad(0.0f),
                 Vec3(0.0f, 1.0f, 0.0f)) *
      Quaternion(static_cast<float>(SDL_atan(z_delta / x_delta)), Vec3(0.0f, 1.0f, 0.0f)) *
      Quaternion(8.0f * corrected_velocity_vector.x, Vec3(1.0f, 0.0f, 0.0f)) *
      Quaternion(-8.0f * corrected_velocity_vector.y, Vec3(0.0f, 0.0f, 1.0f));

  const Mat4x4 world_transform =
      Mat4x4::Translation(ctx->game->player.position) * Mat4x4(orientation) * Mat4x4::Scale(Vec3(0.5f));

  ctx->game->robot_entity.recalculate_node_transforms(ctx->game->materials.robot, world_transform);
}

void monster_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  animate_entity(ctx->game->monster_entity, ctx->engine->generic_allocator, ctx->game->materials.monster,
                 ctx->game->current_time_sec);

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.5f, 0.5f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.001f));

  ctx->game->monster_entity.recalculate_node_transforms(ctx->game->materials.monster, world_transform);
}

void rigged_simple_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  animate_entity(ctx->game->rigged_simple_entity, ctx->engine->generic_allocator, ctx->game->materials.riggedSimple,
                 ctx->game->current_time_sec);

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-5.0f, 6.0f, 0.0f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.5f));

  ctx->game->rigged_simple_entity.recalculate_node_transforms(ctx->game->materials.riggedSimple, world_transform);
}

void moving_lights_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  const float&       time = ctx->game->current_time_sec;
  const LightSource* it   = ctx->game->materials.pbr_light_sources_cache;
  const LightSource* end  = ctx->game->materials.pbr_light_sources_cache_last;
  SimpleEntity*      dst  = ctx->game->box_entities;

  const Quaternion orientation = Quaternion(to_rad(100.0f * time), Vec3(0.0f, 0.0f, 1.0f)) *
                                 Quaternion(to_rad(280.0f * time), Vec3(0.0f, 1.0f, 0.0f)) *
                                 Quaternion(to_rad(60.0f * time), Vec3(1.0f, 0.0f, 0.0f));

  for (; end != it; ++it, ++dst)
  {
    const Mat4x4 world_transform =
        Mat4x4::Translation(it->position.as_vec3()) * Mat4x4(orientation) * Mat4x4::Scale(Vec3(0.05f));

    dst->recalculate_node_transforms(ctx->game->materials.box, world_transform);
  }
}

void matrioshka_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  animate_entity(ctx->game->matrioshka_entity, ctx->engine->generic_allocator, ctx->game->materials.animatedBox,
                 ctx->game->current_time_sec);

  const Quaternion orientation =
      Quaternion(to_rad(90.0f * ctx->game->current_time_sec / 90.0f), Vec3(0.0f, 0.0f, 1.0f)) *
      Quaternion(to_rad(140.0f * ctx->game->current_time_sec / 30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
      Quaternion(to_rad(90.0f * ctx->game->current_time_sec / 20.0f), Vec3(1.0f, 0.0f, 0.0f));

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.0f, 3.0f)) * Mat4x4(orientation);

  ctx->game->matrioshka_entity.recalculate_node_transforms(ctx->game->materials.animatedBox, world_transform);
}

void orientation_axis_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);

  const float rotations[] = {-to_rad(90.0f), -to_rad(90.0f), to_rad(180.0f)};
  const Vec3  axis[]      = {Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)};

  const float translation_offset = 2.0f;
  const Vec3  trans_offsets[]    = {Vec3(translation_offset, 0.0f, 0.0f), Vec3(0.0f, -translation_offset, 0.0f),
                                Vec3(0.0f, 0.0f, translation_offset)};

  for (int i = 0; i < 3; ++i)
  {
    const Mat4x4 world_transform = Mat4x4::Translation(ctx->game->player.position + trans_offsets[i]) *
                                   Mat4x4(Quaternion(rotations[i], axis[i])) * Mat4x4::Scale(Vec3(1.0f, 1.0f, 0.5f));
    ctx->game->axis_arrow_entities[i].recalculate_node_transforms(ctx->game->materials.lil_arrow, world_transform);
  }
}

} // namespace update
