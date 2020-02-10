#include "engine/cascade_shadow_mapping.hh"
#include "game.hh"
#include "story_editor.hh"

namespace {

void update_helmet(SimpleEntity& entity, const SceneGraph& scene_graph)
{
  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(0.0f, 6.0f, 0.0f)) *
                                 Mat4x4(Quaternion(to_rad(180.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(1.6f));
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

Quaternion calculate_player_orientation(const Player& player)
{
  const float x_delta                 = player.position.x - player.camera.position.x;
  const float z_delta                 = player.position.z - player.camera.position.z;
  const Vec2  velocity_vector         = player.velocity.xz();
  const float velocity_angle          = SDL_atan2f(velocity_vector.x, velocity_vector.y);
  const float relative_velocity_angle = player.camera.angle - velocity_angle;

  const Vec2 corrected_velocity_vector =
      Vec2(SDL_cosf(relative_velocity_angle), SDL_sinf(relative_velocity_angle)).scale(velocity_vector.len());

  return Quaternion(to_rad(180.0), Vec3(1.0f, 0.0f, 0.0f)) *
         Quaternion(player.position.x < player.camera.position.x ? to_rad(180.0f) : to_rad(0.0f),
                    Vec3(0.0f, 1.0f, 0.0f)) *
         Quaternion(static_cast<float>(SDL_atan(z_delta / x_delta)), Vec3(0.0f, 1.0f, 0.0f)) *
         Quaternion(8.0f * corrected_velocity_vector.x, Vec3(1.0f, 0.0f, 0.0f)) *
         Quaternion(-8.0f * corrected_velocity_vector.y, Vec3(0.0f, 0.0f, 1.0f));
}

void update_robot(SimpleEntity& entity, const SceneGraph& scene_graph, const Player& player)
{
  const Mat4x4 world_transform =
      Mat4x4::Translation(player.position) * Mat4x4(calculate_player_orientation(player)) * Mat4x4::Scale(Vec3(0.5f));
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_monster(SimpleEntity& entity, const SceneGraph& scene_graph, float current_time_sec)
{
  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.5f, -2.5f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.001f));

  entity.animate(scene_graph, current_time_sec);
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_rigged_simple(SimpleEntity& entity, const SceneGraph& scene_graph, float current_time_sec)
{
  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-5.0f, 6.0f, 0.0f)) *
                                 Mat4x4(Quaternion(to_rad(90.0), Vec3(1.0f, 0.0f, 0.0f))) * Mat4x4::Scale(Vec3(0.5f));

  entity.animate(scene_graph, current_time_sec);
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_moving_light(SimpleEntity& entity, const SceneGraph& scene_graph, const LightSource& light_source,
                         float current_time_sec)
{
  const Quaternion orientation = Quaternion(to_rad(100.0f * current_time_sec), Vec3(0.0f, 0.0f, 1.0f)) *
                                 Quaternion(to_rad(280.0f * current_time_sec), Vec3(0.0f, 1.0f, 0.0f)) *
                                 Quaternion(to_rad(60.0f * current_time_sec), Vec3(1.0f, 0.0f, 0.0f));

  const Mat4x4 world_transform =
      Mat4x4::Translation(light_source.position.as_vec3()) * Mat4x4(orientation) * Mat4x4::Scale(Vec3(0.05f));

  entity.recalculate_node_transforms(scene_graph, world_transform);
  entity.color = light_source.color;
}

void update_matrioshka(SimpleEntity& entity, const SceneGraph& scene_graph, float current_time_sec)
{
  const Quaternion orientation = Quaternion(to_rad(90.0f * current_time_sec / 90.0f), Vec3(0.0f, 0.0f, 1.0f)) *
                                 Quaternion(to_rad(140.0f * current_time_sec / 30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
                                 Quaternion(to_rad(90.0f * current_time_sec / 20.0f), Vec3(1.0f, 0.0f, 0.0f));

  const Mat4x4 world_transform = Mat4x4::Translation(Vec3(-2.0f, 6.0f, 3.0f)) * Mat4x4(orientation);

  entity.animate(scene_graph, current_time_sec);
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_orientation_axis_up(SimpleEntity& entity, const SceneGraph& scene_graph, const Player& player)
{
  float       rotation           = -to_rad(90.0f);
  const Vec3  axis               = Vec3(0.0f, 1.0f, 0.0f);
  const float translation_offset = 2.0f;
  const Vec3  trans_offset       = Vec3(translation_offset, 0.0f, 0.0f);

  const Mat4x4 world_transform = Mat4x4::Translation(player.position + trans_offset) *
                                 Mat4x4(Quaternion(rotation, axis)) * Mat4x4::Scale(Vec3(1.0f, 1.0f, 0.5f));
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_orientation_axis_left(SimpleEntity& entity, const SceneGraph& scene_graph, const Player& player)
{
  const float rotation           = -to_rad(90.0f);
  const Vec3  axis               = Vec3(1.0f, 0.0f, 0.0f);
  const float translation_offset = 2.0f;
  const Vec3  trans_offset       = Vec3(0.0f, -translation_offset, 0.0f);

  const Mat4x4 world_transform = Mat4x4::Translation(player.position + trans_offset) *
                                 Mat4x4(Quaternion(rotation, axis)) * Mat4x4::Scale(Vec3(1.0f, 1.0f, 0.5f));
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

void update_orientation_axis_right(SimpleEntity& entity, const SceneGraph& scene_graph, const Player& player)
{
  const float rotation           = to_rad(180.0f);
  const Vec3  axis               = Vec3(1.0f, 0.0f, 0.0f);
  const float translation_offset = 2.0f;
  const Vec3  trans_offset       = Vec3(0.0f, 0.0f, translation_offset);

  const Mat4x4 world_transform = Mat4x4::Translation(player.position + trans_offset) *
                                 Mat4x4(Quaternion(rotation, axis)) * Mat4x4::Scale(Vec3(1.0f, 1.0f, 0.5f));
  entity.recalculate_node_transforms(scene_graph, world_transform);
}

struct UpdateJob
{
  UpdateJob(JobContext& ctx, uint32_t thread_id, const char* name)
      : game(*ctx.game)
      , level(game.level)
      , perf_event(game.update_profiler, name, thread_id)
  {
  }

  UpdateJob(ThreadJobData& tjd, const char* name)
      : UpdateJob(*reinterpret_cast<JobContext*>(tjd.user_data), tjd.thread_id, name)
  {
  }

  Game&           game;
  ExampleLevel&   level;
  ScopedPerfEvent perf_event;
};

void helmet_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_helmet(ctx.level.helmet_entity, ctx.game.materials.helmet);
}

void robot_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_robot(ctx.level.robot_entity, ctx.game.materials.robot, ctx.game.player);
}

void monster_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_monster(ctx.level.monster_entity, ctx.game.materials.monster, ctx.game.current_time_sec);
}

void rigged_simple_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_rigged_simple(ctx.level.rigged_simple_entity, ctx.game.materials.riggedSimple, ctx.game.current_time_sec);
}

void moving_lights_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);

  const float acceleration_length = 5.0f * 1000.0f * ctx.game.player.acceleration.len();

  //
  // engines precalculation
  //
  const Mat4x4 player_rotation = Mat4x4(calculate_player_orientation(ctx.game.player));
  const Mat4x4 transform_a =
      Mat4x4::Translation(ctx.game.player.position) * player_rotation * Mat4x4::Translation(Vec3(-0.2f, 0.4f, -0.35f));
  const Mat4x4 transform_b =
      Mat4x4::Translation(ctx.game.player.position) * player_rotation * Mat4x4::Translation(Vec3(-0.2f, 0.4f, 0.35f));

  LightSource dynamic_lights[] = {
      {
          {SDL_sinf(ctx.game.current_time_sec), 0.0f, 3.0f + SDL_cosf(ctx.game.current_time_sec), 1.0f},
          {20.0f + (5.0f * SDL_sinf(ctx.game.current_time_sec + 0.4f)), 0.0f, 0.0f, 1.0f},
      },
      {
          {12.8f * SDL_cosf(ctx.game.current_time_sec), 0.0f, -10.0f + (8.8f * SDL_sinf(ctx.game.current_time_sec)),
           1.0f},
          {0.0f, 20.0f, 0.0f, 1.0f},
      },
      {
          {20.8f * SDL_sinf(ctx.game.current_time_sec / 2.0f), 0.0f,
           3.0f + (0.8f * SDL_cosf(ctx.game.current_time_sec / 2.0f)), 1.0f},
          {0.0f, 0.0f, 20.0f, 1.0f},
      },
      {
          {SDL_sinf(ctx.game.current_time_sec / 1.2f), 0.0f, 2.5f * SDL_cosf(ctx.game.current_time_sec / 1.2f), 1.0f},
          {8.0f, 8.0f, 8.0f, 1.0f},
      },
      {
          {0.0f, 0.0f, -4.0f, 1.0f},
          {10.0f, 0.0f, 10.0f, 1.0f},
      },
      // player engines
      {
          Vec4(transform_a.get_position(), 1.0f),
          {0.01f, 0.01f, acceleration_length, 1.0f},
      },
      {
          Vec4(transform_b.get_position(), 1.0f),
          {0.01f, 0.01f, acceleration_length, 1.0f},
      },
  };

  for (uint32_t i = 0; i < 5; ++i)
  {
    LightSource& light = dynamic_lights[i];
    light.position.y   = ctx.game.level.get_height(light.position.x, light.position.z) - 1.0f;
  }

  SDL_LockMutex(ctx.game.materials.pbr_light_sources_cache_lock);
  ctx.game.materials.pbr_light_sources_cache.push(dynamic_lights, dynamic_lights + array_size(dynamic_lights));
  SDL_UnlockMutex(ctx.game.materials.pbr_light_sources_cache_lock);

  SimpleEntity* dst = ctx.level.box_entities;
  for (uint32_t i = 0; i < array_size(dynamic_lights); ++i)
  {
    update_moving_light(dst[i], ctx.game.materials.box, dynamic_lights[i], ctx.game.current_time_sec);
  }
}

void matrioshka_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_matrioshka(ctx.level.matrioshka_entity, ctx.game.materials.animatedBox, ctx.game.current_time_sec);
}

void orientation_axis_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  update_orientation_axis_up(ctx.level.axis_arrow_entities[0], ctx.game.materials.lil_arrow, ctx.game.player);
  update_orientation_axis_left(ctx.level.axis_arrow_entities[1], ctx.game.materials.lil_arrow, ctx.game.player);
  update_orientation_axis_right(ctx.level.axis_arrow_entities[2], ctx.game.materials.lil_arrow, ctx.game.player);
}

void gui_lines_generation_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);

  //
  // in vulkan coordinate system Y axis is pointing down, so we'll have to invert the value to get
  // something more reasonable
  //
  GenerateGuiLinesCommand cmd = {
      .player_y_location_meters = -ctx.game.player.position.y,
      .camera_x_pitch_radians   = 0.0f, // to_rad(10) * SDL_sinf(current_time_sec), // simulating future strafe tilts,
      .camera_y_pitch_radians   = ctx.game.player.get_camera().updown_angle,
  };

  generate_gui_lines(cmd, ctx.game.materials.gui_lines_memory_cache, MAX_ROBOT_GUI_LINES,
                     ctx.game.materials.gui_green_lines_count, ctx.game.materials.gui_red_lines_count,
                     ctx.game.materials.gui_yellow_lines_count);
}

void recalculate_csm_matrices(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  recalculate_cascade_view_proj_matrices(ctx.game.materials.cascade_view_proj_mat,
                                         ctx.game.materials.cascade_split_depths, ctx.game.player.camera_projection,
                                         ctx.game.player.camera_view, ctx.game.materials.light_source_position);
}

void story_job(ThreadJobData tjd)
{
  UpdateJob ctx(tjd, __FUNCTION__);
  ctx.game.story.tick(ctx.game.player, tjd.allocator);
}

} // namespace

Job* ExampleLevel::copy_update_jobs(Job* dst)
{
  const Job jobs[] = {monster_job,              //
                      helmet_job,               //
                      robot_job,                //
                      rigged_simple_job,        //
                      moving_lights_job,        //
                      matrioshka_job,           //
                      orientation_axis_job,     //
                      gui_lines_generation_job, //
                      recalculate_csm_matrices, //
                      story_job};
  return std::copy(jobs, &jobs[SDL_arraysize(jobs)], dst);
}
