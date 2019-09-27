#include "update_jobs.hh"
#include <SDL2/SDL_log.h>

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
  SimpleEntity&   entity = ctx->game->monster_entity;

  entity.animate(ctx->engine->generic_allocator, ctx->game->materials.monster, ctx->game->current_time_sec);

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.5f, 0.5f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.001f));

  entity.recalculate_node_transforms(ctx->game->materials.monster, world_transform);
}

void rigged_simple_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->update_profiler, __PRETTY_FUNCTION__, tjd.thread_id);
  SimpleEntity&   entity = ctx->game->rigged_simple_entity;

  entity.animate(ctx->engine->generic_allocator, ctx->game->materials.riggedSimple, ctx->game->current_time_sec);

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-5.0f, 6.0f, 0.0f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.5f));

  entity.recalculate_node_transforms(ctx->game->materials.riggedSimple, world_transform);
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
  SimpleEntity&   entity = ctx->game->matrioshka_entity;

  entity.animate(ctx->engine->generic_allocator, ctx->game->materials.animatedBox, ctx->game->current_time_sec);

  const Quaternion orientation =
      Quaternion(to_rad(90.0f * ctx->game->current_time_sec / 90.0f), Vec3(0.0f, 0.0f, 1.0f)) *
      Quaternion(to_rad(140.0f * ctx->game->current_time_sec / 30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
      Quaternion(to_rad(90.0f * ctx->game->current_time_sec / 20.0f), Vec3(1.0f, 0.0f, 0.0f));

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.0f, 3.0f)) * Mat4x4(orientation);

  entity.recalculate_node_transforms(ctx->game->materials.animatedBox, world_transform);
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
