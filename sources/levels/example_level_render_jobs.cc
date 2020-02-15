#include "engine/aligned_push_consts.hh"
#include "engine/memory_map.hh"
#include "game.hh"
#include "game_generate_sdf_font.hh"
#include "game_render_entity.hh"
#include <SDL2/SDL_log.h>

namespace {

VkCommandBuffer acquire_command_buffer(ThreadJobData& tjd)
{
  JobContext* ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  return ctx->engine->job_system.acquire(tjd.thread_id, ctx->game->image_index);
}

[[maybe_unused]] void render_skybox(VkCommandBuffer command, VkBuffer buffer, const Player& player,
                                    const Pipelines::Pair& pipe, const Materials& materials)
{
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout, 0, 1, &materials.skybox_cubemap_dset,
                          0, nullptr);

  AlignedPushConsts(command, pipe.layout)
      .push(VK_SHADER_STAGE_VERTEX_BIT, player.camera_projection)
      .push(VK_SHADER_STAGE_VERTEX_BIT, player.camera_view);

  const Node& node = materials.box.nodes.data[1];
  const Mesh& mesh = materials.box.meshes.data[node.mesh];

  vkCmdBindIndexBuffer(command, buffer, mesh.indices_offset, mesh.indices_type);
  vkCmdBindVertexBuffers(command, 0, 1, &buffer, &mesh.vertices_offset);
  vkCmdDrawIndexed(command, mesh.indices_count, 1, 0, 0, 0);
}

void skybox_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command   = acquire_command_buffer(tjd);
  ctx->game->skybox_command = command;
  ctx->engine->render_passes.skybox.begin(command, ctx->game->image_index);

  struct
  {
    Mat4x4 projection;
    Mat4x4 view;
  } push = {};

  push.projection = ctx->game->player.camera_projection;
  push.view       = ctx->game->player.camera_view;

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.skybox.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.skybox.layout, 0, 1,
                          &ctx->game->materials.skybox_cubemap_dset, 0, nullptr);
  vkCmdPushConstants(command, ctx->engine->pipelines.skybox.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

  const Node& node = ctx->game->materials.box.nodes.data[1];
  Mesh&       mesh = ctx->game->materials.box.meshes.data[node.mesh];

  vkCmdBindIndexBuffer(command, ctx->engine->gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer, &mesh.vertices_offset);
  vkCmdDrawIndexed(command, mesh.indices_count, 1, 0, 0, 0);

  vkEndCommandBuffer(command);
}

void robot_depth_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
  {
    VkCommandBuffer command = acquire_command_buffer(tjd);
    ctx->game->shadow_mapping_pass_commands.push({command, cascade_idx});
    ctx->engine->render_passes.shadowmap.begin(command, static_cast<uint32_t>(cascade_idx));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.shadowmap.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.shadowmap.layout, 0, 1,
                            &ctx->game->materials.cascade_view_proj_matrices_depth_pass_dset[ctx->game->image_index], 0,
                            nullptr);
    render_pbr_entity_shadow(ctx->game->level.robot_entity, ctx->game->materials.robot, *ctx->engine, *ctx->game,
                             command, cascade_idx);
    vkEndCommandBuffer(command);
  }
}

void robot_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.pipeline);

  {
    const Materials& mats    = ctx->game->materials;
    VkDescriptorSet  dsets[] = {
        mats.robot_pbr_material_dset,
        mats.pbr_ibl_environment_dset,
        mats.debug_shadow_map_dset,
        mats.pbr_dynamic_lights_dset,
        mats.cascade_view_proj_matrices_render_dset[ctx->game->image_index],
    };

    uint32_t dynamic_offsets[] = {
        static_cast<uint32_t>(ctx->game->materials.pbr_dynamic_lights_ubo_offsets[ctx->game->image_index])};

    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.layout, 0,
                            array_size(dsets), dsets, array_size(dynamic_offsets), dynamic_offsets);
  }

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(0.0f, 0.0f, 0.0f);
  params.pipeline_layout = ctx->engine->pipelines.scene3D.layout;

  render_pbr_entity(ctx->game->level.robot_entity, ctx->game->materials.robot, *ctx->engine, params);

#if 0
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_model_wireframe.pipeline);
  params.pipeline_layout = ctx->engine->pipelines.colored_model_wireframe.layout;

  params.color[0] = SDL_fabsf(SDL_sinf(ctx->game->current_time_sec));
  params.color[1] = SDL_fabsf(SDL_cosf(ctx->game->current_time_sec * 1.2f));
  params.color[2] = SDL_fabsf(SDL_sinf(1.0f * ctx->game->current_time_sec * 1.5f));

  render_wireframe_entity(ctx->game->robot_entity, ctx->game->materials.robot, *ctx->engine, params);
#endif

  vkEndCommandBuffer(command);
}

void helmet_depth_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
  {
    VkCommandBuffer command = acquire_command_buffer(tjd);
    ctx->game->shadow_mapping_pass_commands.push({command, cascade_idx});
    ctx->engine->render_passes.shadowmap.begin(command, static_cast<uint32_t>(cascade_idx));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.shadowmap.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.shadowmap.layout, 0, 1,
                            &ctx->game->materials.cascade_view_proj_matrices_depth_pass_dset[ctx->game->image_index], 0,
                            nullptr);
    render_pbr_entity_shadow(ctx->game->level.helmet_entity, ctx->game->materials.helmet, *ctx->engine, *ctx->game,
                             command, cascade_idx);
    vkEndCommandBuffer(command);
  }
}

void helmet_job(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.pipeline);

  {
    VkDescriptorSet dsets[] = {
        ctx->game->materials.helmet_pbr_material_dset,
        ctx->game->materials.pbr_ibl_environment_dset,
        ctx->game->materials.debug_shadow_map_dset,
        ctx->game->materials.pbr_dynamic_lights_dset,
        ctx->game->materials.cascade_view_proj_matrices_render_dset[ctx->game->image_index],
    };

    uint32_t dynamic_offsets[] = {
        static_cast<uint32_t>(ctx->game->materials.pbr_dynamic_lights_ubo_offsets[ctx->game->image_index])};

    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.layout, 0,
                            array_size(dsets), dsets, array_size(dynamic_offsets), dynamic_offsets);
  }

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(0.0f, 0.0f, 0.0f);
  params.pipeline_layout = ctx->engine->pipelines.scene3D.layout;

  render_pbr_entity(ctx->game->level.helmet_entity, ctx->game->materials.helmet, *ctx->engine, params);

  vkEndCommandBuffer(command);
}

void point_light_boxes(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry.pipeline);

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(0.0f, 0.0f, 0.0f);
  params.pipeline_layout = ctx->engine->pipelines.colored_geometry.layout;

  for (const SimpleEntity& entity : ctx->game->level.box_entities)
  {
    params.color = entity.color.as_vec3();
    render_entity(entity, ctx->game->materials.box, *ctx->engine, params);
  }

  if (ctx->game->story.is_point_requested_to_render)
  {
    params.color                 = Vec3(1.0, 0.1, 0.1);
    const Mat4x4 world_transform = Mat4x4::Translation(ctx->game->story.point_to_render) * Mat4x4::Scale(Vec3(1.0f));
    ctx->game->level.inspected_story_point.recalculate_node_transforms(ctx->game->materials.box, world_transform);
    render_entity(ctx->game->level.inspected_story_point, ctx->game->materials.box, *ctx->engine, params);
  }

  vkEndCommandBuffer(command);
}

void matrioshka_box(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry.pipeline);

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(0.0f, 1.0f, 0.0f);
  params.pipeline_layout = ctx->engine->pipelines.colored_geometry.layout;

  render_entity(ctx->game->level.matrioshka_entity, ctx->game->materials.animatedBox, *ctx->engine, params);

  vkEndCommandBuffer(command);
}

[[maybe_unused]] void vr_scene(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.pipeline);

  {
    VkDescriptorSet dsets[] = {
        ctx->game->materials.sandy_level_pbr_material_dset,
        ctx->game->materials.pbr_ibl_environment_dset,
        ctx->game->materials.debug_shadow_map_dset,
        ctx->game->materials.pbr_dynamic_lights_dset,
        ctx->game->materials.cascade_view_proj_matrices_render_dset[ctx->game->image_index],
    };

    uint32_t dynamic_offsets[] = {
        static_cast<uint32_t>(ctx->game->materials.pbr_dynamic_lights_ubo_offsets[ctx->game->image_index])};

    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.scene3D.layout, 0,
                            array_size(dsets), dsets, array_size(dynamic_offsets), dynamic_offsets);
  }

  vkCmdBindIndexBuffer(command, ctx->engine->gpu_device_local_memory_buffer,
                       ctx->game->materials.vr_level_index_buffer_offset, ctx->game->materials.vr_level_index_type);

  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.vr_level_vertex_buffer_offset);

  Mat4x4 translation_matrix;
  translation_matrix.translate(Vec3(0.0, 3.0, 0.0));

  Mat4x4 rotation_matrix;
  rotation_matrix.identity();

  Mat4x4 scale_matrix;
  scale_matrix.identity();
  scale_matrix.scale(Vec3(100.0f, 100.0f, 100.0f));

  struct SkinningUbo
  {
    Mat4x4 projection;
    Mat4x4 view;
    Mat4x4 model;
    Vec3   camera_position;
  } ubo;

  ubo.projection      = ctx->game->player.camera_projection;
  ubo.view            = ctx->game->player.camera_view;
  ubo.model           = translation_matrix * rotation_matrix * scale_matrix;
  ubo.camera_position = ctx->game->player.get_camera().position;

  vkCmdPushConstants(command, ctx->engine->pipelines.scene3D.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ubo), &ubo);
  vkCmdDrawIndexed(command, static_cast<uint32_t>(ctx->game->materials.vr_level_index_count), 1, 0, 0, 0);

  vkEndCommandBuffer(command);
}

#if 0
    void vr_scene_depth(ThreadJobData tjd)
{
  for (int cascade_idx = 0; cascade_idx < Engine::SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
  {
    VkCommandBuffer command = acquire_command_buffer(tjd);
    ctx->game->shadow_mapping_pass_commands.push({command, cascade_idx});

    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = ctx->engine->shadowmap_render_pass,
        .framebuffer = ctx->engine->shadowmap_framebuffers[ctx->game->image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(command, &begin_info);

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, 0.0, 3.0, 0.0);

    mat4x4 rotation_matrix = {};
    mat4x4_identity(rotation_matrix);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    const float scale = 100.0f;
    mat4x4_scale_aniso(scale_matrix, scale_matrix, scale, scale, scale);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);

    struct PushConstant
    {
      mat4x4 light_space_matrix;
      mat4x4 model;
    } pc = {};

    mat4x4_dup(pc.light_space_matrix, ctx->game->light_space_matrix);
    mat4x4_mul(pc.model, tmp, scale_matrix);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->shadow_mapping.pipeline);
    vkCmdBindIndexBuffer(command, ctx->engine->gpu_device_local_memory_buffer, ctx->game->vr_level_index_buffer_offset,
                         ctx->game->vr_level_index_type);
    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                           &ctx->game->vr_level_vertex_buffer_offset);
    vkCmdPushConstants(command, ctx->engine->shadow_mapping.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc),
                       &pc);
    vkCmdDrawIndexed(command, static_cast<uint32_t>(ctx->game->vr_level_index_count), 1, 0, 0, 0);

    vkEndCommandBuffer(command);
  }
}
#endif

void simple_rigged(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry_skinned.pipeline);

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(ctx->game->materials.rig_skinning_matrices_ubo_offsets[ctx->game->image_index])};

  vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry_skinned.layout, 0, 1,
      &ctx->game->materials.rig_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(0.0f, 0.0f, 0.0f);
  params.pipeline_layout = ctx->engine->pipelines.colored_geometry_skinned.layout;

  render_entity_skinned(ctx->game->level.rigged_simple_entity, ctx->game->materials.riggedSimple, *ctx->engine, params);

  vkEndCommandBuffer(command);
}

void monster_rigged(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry_skinned.pipeline);

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(ctx->game->materials.monster_skinning_matrices_ubo_offsets[ctx->game->image_index])};

  vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry_skinned.layout, 0, 1,
      &ctx->game->materials.monster_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.color           = Vec3(1.0f, 1.0f, 1.0f);
  params.pipeline_layout = ctx->engine->pipelines.colored_geometry_skinned.layout;

  render_entity_skinned(ctx->game->level.monster_entity, ctx->game->materials.monster, *ctx->engine, params);

  vkEndCommandBuffer(command);
}

void radar(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui.pipeline);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

  Mat4x4 gui_projection;
  gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

  const float rectangle_dimension_pixels = 100.0f;
  const float offset_from_edge           = 10.0f;

  const Mat4x4 mvp = gui_projection *
                     Mat4x4::Translation({rectangle_dimension_pixels + offset_from_edge,
                                          rectangle_dimension_pixels + offset_from_edge, -1.0f}) *
                     Mat4x4::Scale({rectangle_dimension_pixels, rectangle_dimension_pixels, 1.0f});

  AlignedPushConsts(command, ctx->engine->pipelines.green_gui.layout)
      .push(VK_SHADER_STAGE_VERTEX_BIT, mvp)
      .push(VK_SHADER_STAGE_FRAGMENT_BIT, ctx->game->current_time_sec);

  vkCmdDraw(command, 4, 1, 0, 0);
  vkEndCommandBuffer(command);
}

void robot_gui_lines(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_lines.pipeline);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_host_coherent_memory_buffer,
                         &ctx->game->materials.green_gui_rulers_buffer_offsets[ctx->game->image_index]);

  ctx->game->level.lines_renderer.render(command, ctx->engine->pipelines.green_gui_lines.layout);

#if 0
  uint32_t offset = 0;

  // ------ GREEN ------
  {
    VkRect2D scissor{.extent = ctx->engine->extent2D};
    vkCmdSetScissor(command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = ctx->game->materials.gui_green_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    Vec4 color(Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f), 0.9f);

    vkCmdPushConstants(command, ctx->engine->pipelines.green_gui_lines.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(Vec4), color.data());

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(command, line_widths[i]);
      vkCmdDraw(command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ RED ------
  {
    VkRect2D scissor{};
    scissor.extent.width  = ctx->engine->to_pixel_length_x(1.5f);
    scissor.extent.height = ctx->engine->to_pixel_length_y(1.02f);
    scissor.offset.x      = (ctx->engine->extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = ctx->engine->to_pixel_length_y(0.29f);
    vkCmdSetScissor(command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = ctx->game->materials.gui_red_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    Vec4 color(1.0f, 0.0f, 0.0f, 0.9f);
    vkCmdPushConstants(command, ctx->engine->pipelines.green_gui_lines.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(Vec4), color.data());

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(command, line_widths[i]);
      vkCmdDraw(command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ YELLOW ------
  {
    VkRect2D scissor      = {};
    scissor.extent.width  = ctx->engine->to_pixel_length_x(0.5f);
    scissor.extent.height = ctx->engine->to_pixel_length_y(1.3f);
    scissor.offset.x      = (ctx->engine->extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = ctx->engine->to_pixel_length_y(0.2f);
    vkCmdSetScissor(command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = ctx->game->materials.gui_yellow_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    Vec4 color(1.0f, 1.0f, 0.0f, 0.7f);
    vkCmdPushConstants(command, ctx->engine->pipelines.green_gui_lines.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(Vec4), color.data());

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(command, line_widths[i]);
      vkCmdDraw(command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }
#endif

  vkEndCommandBuffer(command);
}

void robot_gui_speed_meter_text(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout, 0,
                          1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    Mat4x4 mvp;
    Vec2   character_coordinate;
    Vec2   character_size;
  } vpc;

  struct FragmentPushConstant
  {
    Vec3  color;
    float time = 0.0f;
  } fpc;

  fpc.time = ctx->game->current_time_sec;

  {
    Mat4x4 gui_projection;
    gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

    float speed     = ctx->game->player.velocity.len() * 1500.0f;
    int   speed_int = static_cast<int>(speed);

    auto count_and_substract = [&speed_int](const int counted) -> char {
      int r = speed_int / counted;
      speed_int -= counted * r;
      return static_cast<char>(r);
    };

    char text_form[] = {
        char('0' + count_and_substract(1000)),
        char('0' + count_and_substract(100)),
        char('0' + count_and_substract(10)),
        char('0' + static_cast<char>(speed_int)),
    };

    float cursor = 0.0f;

    for (const char c : text_form)
    {
      GenerateSdfFontCommand cmd = {
          .character             = c,
          .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
          .texture_size          = {512.0f, 256.0f},
          .scaling               = ctx->engine->extent2D.height / 4.1f,
          .position =
              {
                  static_cast<float>(ctx->engine->to_pixel_length_x(0.48f)),
                  static_cast<float>(ctx->engine->to_pixel_length_y(0.80f)),
                  -1.0f,
              },
          .cursor = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      vpc.character_coordinate = r.character_coordinate;
      vpc.character_size       = r.character_size;
      vpc.mvp                  = gui_projection * r.transform;
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = ctx->engine->extent2D};
      vkCmdSetScissor(command, 0, 1, &scissor);

      fpc.color = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);

      AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(vpc), &vpc)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(fpc), &fpc);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
} // namespace render

void robot_gui_speed_meter_triangle(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_triangle.pipeline);

  const Vec4 offset = Vec4(-0.384f, -0.180f, 0.0f, 0.0f);
  const Vec4 scale  = Vec4(0.012f, 0.02f, 1.0f, 1.0f);
  const Vec4 color  = Vec4(Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f), 1.0f);

  AlignedPushConsts(command, ctx->engine->pipelines.green_gui_triangle.layout)
      .push(VK_SHADER_STAGE_VERTEX_BIT, offset)
      .push(VK_SHADER_STAGE_VERTEX_BIT, scale)
      .push(VK_SHADER_STAGE_FRAGMENT_BIT, color);

  vkCmdDraw(command, 3, 1, 0, 0);
  vkEndCommandBuffer(command);
}

void height_ruler_text(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout, 0,
                          1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

#if 0
        struct VertexPushConstant
  {
    Mat4x4 mvp;
    Vec2   character_coordinate;
    Vec2   character_size;
  } vpc;

  struct FragmentPushConstant
  {
    Vec3  color;
    float time;
  } fpc;
#endif

  const float time  = ctx->game->current_time_sec;
  const Vec3  color = Vec3(1.0f, 0.0f, 0.0f);

  //--------------------------------------------------------------------------
  // height rulers values
  //--------------------------------------------------------------------------

  GenerateGuiLinesCommand cmd = {
      .player_y_location_meters = -(2.0f - ctx->game->player.position.y),
      .camera_x_pitch_radians   = ctx->game->player.get_camera().angle,
      .camera_y_pitch_radians   = ctx->game->player.get_camera().angle,
      .screen_extent2D          = ctx->engine->extent2D,
  };

  ArrayView<GuiHeightRulerText> scheduled_text_data = generate_gui_height_ruler_text(cmd, tjd.allocator);

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    Mat4x4 gui_projection;
    gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
          .texture_size          = {512.0f, 256.0f},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset.x, text.offset.y, -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      // vpc.mvp                  = gui_projection * r.transform;

      const Mat4x4 mvp                  = gui_projection * r.transform;
      const Vec2   character_coordinate = r.character_coordinate;
      const Vec2   character_size       = r.character_size;

      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = ctx->engine->to_pixel_length_x(0.75f);
      scissor.extent.height = ctx->engine->to_pixel_length_y(1.02f);
      scissor.offset.x      = (ctx->engine->extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = ctx->engine->to_pixel_length_y(0.29f);
      vkCmdSetScissor(command, 0, 1, &scissor);

      AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, mvp)
          .push(VK_SHADER_STAGE_VERTEX_BIT, character_coordinate)
          .push(VK_SHADER_STAGE_VERTEX_BIT, character_size)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, color)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, time);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
}

void tilt_ruler_text(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout, 0,
                          1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    Mat4x4 mvp;
    Vec2   character_coordinate;
    Vec2   character_size;
  } vpc;

  struct FragmentPushConstant
  {
    Vec3  color;
    float time = 0.0f;
  } fpc;

  fpc.time = ctx->game->current_time_sec;

  //--------------------------------------------------------------------------
  // tilt rulers values
  //--------------------------------------------------------------------------

  GenerateGuiLinesCommand cmd = {
      .player_y_location_meters = -(2.0f - ctx->game->player.position.y),
      .camera_x_pitch_radians   = ctx->game->player.get_camera().angle,
      .camera_y_pitch_radians   = ctx->game->player.get_camera().angle,
      .screen_extent2D          = ctx->engine->extent2D,
  };

  ArrayView<GuiHeightRulerText> scheduled_text_data = generate_gui_tilt_ruler_text(cmd, tjd.allocator);

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    Mat4x4 gui_projection;
    gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
          .texture_size          = {512.0f, 256.0f},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset.x, text.offset.y, -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      vpc.character_coordinate = r.character_coordinate;
      vpc.character_size       = r.character_size;
      vpc.mvp                  = gui_projection * r.transform;
      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = ctx->engine->to_pixel_length_x(0.5f);
      scissor.extent.height = ctx->engine->to_pixel_length_y(1.3f);
      scissor.offset.x      = (ctx->engine->extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = ctx->engine->to_pixel_length_y(0.2f);
      vkCmdSetScissor(command, 0, 1, &scissor);

      fpc.color = Vec3(1.0f, 1.0f, 0.0f);

      AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(vpc), &vpc)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(fpc), &fpc);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
}

void compass_text(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout, 0,
                          1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    Mat4x4 mvp;
    Vec2   character_coordinate;
    Vec2   character_size;
  } vpc;

  struct FragmentPushConstant
  {
    Vec3  color;
    float time = 0.0f;
  } fpc;

  fpc.time = ctx->game->current_time_sec;

  const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

  const float direction_increment = to_rad(22.5f);

  float angle_mod = ctx->game->player.get_camera().angle + (0.5f * direction_increment);
  if (angle_mod > (2 * M_PI))
    angle_mod -= (2 * M_PI);

  unsigned direction_iter = 0;
  while (angle_mod > direction_increment)
  {
    direction_iter += 1;
    angle_mod -= direction_increment;
  }

  const unsigned left_direction_iter = (0 == direction_iter) ? (SDL_arraysize(directions) - 1u) : (direction_iter - 1u);
  const unsigned right_direction_iter = ((SDL_arraysize(directions) - 1) == direction_iter) ? 0u : direction_iter + 1u;

  const char* center_text = directions[direction_iter];
  const char* left_text   = directions[left_direction_iter];
  const char* right_text  = directions[right_direction_iter];

  Mat4x4 gui_projection;
  gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);
  float cursor = 0.0f;

  //////////////////////////////////////////////////////////////////////////////
  // CENTER TEXT RENDERING
  //////////////////////////////////////////////////////////////////////////////
  for (unsigned i = 0; i < SDL_strlen(center_text); ++i)
  {
    const char c = center_text[i];

    if ('\0' == c)
      continue;

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
        .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
        .texture_size          = {512.0f, 256.0f},
        .scaling               = 300.0f,
        .position =
            {
                static_cast<float>(ctx->engine->to_pixel_length_x(1.0f - angle_mod + (0.5f * direction_increment))),
                static_cast<float>(ctx->engine->to_pixel_length_y(1.335f)),
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    vpc.character_coordinate = r.character_coordinate;
    vpc.character_size       = r.character_size;
    vpc.mvp                  = gui_projection * r.transform;
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = ctx->engine->extent2D};
    vkCmdSetScissor(command, 0, 1, &scissor);

    fpc.color = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);

    AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, vpc)
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, fpc);

    vkCmdDraw(command, 4, 1, 0, 0);
  }

  cursor = 0.0f;

  //////////////////////////////////////////////////////////////////////////////
  // LEFT TEXT RENDERING
  //////////////////////////////////////////////////////////////////////////////
  for (unsigned i = 0; i < SDL_strlen(left_text); ++i)
  {
    const char c = left_text[i];

    if ('\0' == c)
      continue;

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
        .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
        .texture_size          = {512.0f, 256.0f},
        .scaling               = 200.0f, // ctx->game->DEBUG_VEC2[0],
        .position =
            {
                static_cast<float>(ctx->engine->to_pixel_length_x(0.8f)),
                static_cast<float>(ctx->engine->to_pixel_length_y(1.345f)),
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    vpc.character_coordinate = r.character_coordinate;
    vpc.character_size       = r.character_size;
    vpc.mvp                  = gui_projection * r.transform;
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = ctx->engine->extent2D};
    vkCmdSetScissor(command, 0, 1, &scissor);

    fpc.color = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);

    AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(vpc), &vpc)
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(fpc), &fpc);

    vkCmdDraw(command, 4, 1, 0, 0);
  }

  cursor = 0.0f;

  //////////////////////////////////////////////////////////////////////////////
  // RIGHT TEXT RENDERING
  //////////////////////////////////////////////////////////////////////////////
  for (unsigned i = 0; i < SDL_strlen(right_text); ++i)
  {
    const char c = right_text[i];

    if ('\0' == c)
      continue;

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
        .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
        .texture_size          = {512, 256},
        .scaling               = 200.0f, // ctx->game->DEBUG_VEC2[0],
        .position =
            {
                static_cast<float>(ctx->engine->to_pixel_length_x(1.2f)),
                static_cast<float>(ctx->engine->to_pixel_length_y(1.345f)),
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    vpc.character_coordinate = r.character_coordinate;
    vpc.character_size       = r.character_size;
    vpc.mvp                  = gui_projection * r.transform;
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = ctx->engine->extent2D};
    vkCmdSetScissor(command, 0, 1, &scissor);

    fpc.color = Vec3(125.0f, 204.0f, 174.0f).scale(1.0f / 255.0f);

    AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(vpc), &vpc)
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(fpc), &fpc);

    vkCmdDraw(command, 4, 1, 0, 0);
  }

  vkEndCommandBuffer(command);
}

void radar_dots(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_radar_dots.pipeline);

  uint32_t rectangle_dim           = 100u;
  float    vertical_length         = ctx->engine->to_line_length_x(rectangle_dim);
  float    offset_from_screen_edge = ctx->engine->to_line_length_x(rectangle_dim / 10);

  const float horizontal_length    = ctx->engine->to_line_length_y(rectangle_dim);
  const float offset_from_top_edge = ctx->engine->to_line_length_y(rectangle_dim / 10);

  const Vec2 center_radar_position(-1.0f + offset_from_screen_edge + vertical_length,
                                   -1.0f + offset_from_top_edge + horizontal_length);

  Vec2 robot_position  = Vec2(0.0f, 0.0f);
  Vec2 player_position = ctx->game->player.position.xz();

  // players position becomes the cartesian (0, 0) point for us, hence the substraction order
  Vec2 distance   = robot_position - player_position;
  Vec2 normalized = distance.normalize();

  float      robot_angle     = SDL_atan2f(normalized.x, normalized.y);
  float      angle           = ctx->game->player.get_camera().angle - robot_angle - (M_PI_2);
  float      final_distance  = 0.005f * distance.len();
  float      aspect_ratio    = vertical_length / horizontal_length;
  const Vec2 helmet_position = {aspect_ratio * final_distance * SDL_sinf(angle), final_distance * SDL_cosf(angle)};

  const Vec2 relative_helmet_position = center_radar_position - helmet_position;

  Vec4 position = {relative_helmet_position.x, relative_helmet_position.y, 0.0f, 1.0f};
  Vec4 color    = Vec4(1.0f, 0.0f, 0.0f, (final_distance < 0.22f) ? 0.6f : 0.0f);

  AlignedPushConsts(command, ctx->engine->pipelines.green_gui_radar_dots.layout)
      .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Vec4), position.data())
      .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Vec4), color.data());

  vkCmdDraw(command, 1, 1, 0, 0);
  vkEndCommandBuffer(command);
}

void weapon_selectors_left(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = ctx->engine->render_passes.gui.render_pass,
        .framebuffer = ctx->engine->render_passes.gui.framebuffers[ctx->game->image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(command, &begin_info);
  }

  Mat4x4 gui_projection;
  gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

  const Vec2 screen_extent           = {(float)ctx->engine->extent2D.width, (float)ctx->engine->extent2D.height};
  const Vec2 box_size                = {120.0f, 25.0f};
  const Vec2 offset_from_bottom_left = {25.0f, 25.0f};

  float transparencies[3];
  ctx->game->level.weapon_selections[0].calculate(transparencies);

  for (int i = 0; i < 3; ++i)
  {
    ////////////////////////////////////////////////////////////////////////////
    // Bordered box for the text inside
    ////////////////////////////////////////////////////////////////////////////

    const Vec2 translation =
        Vec2(box_size.x + offset_from_bottom_left.x + (14.0f * static_cast<float>(i)),
             screen_extent.y - (box_size.y * 2.00f * static_cast<float>(i + 1)) - offset_from_bottom_left.y);

    const Mat4x4 mvp =
        gui_projection *
        Mat4x4::Translation(Vec3(
            box_size.x + offset_from_bottom_left.x + (14.0f * static_cast<float>(i)),
            screen_extent.y - (box_size.y * 2.00f * static_cast<float>(i + 1)) - offset_from_bottom_left.y, -1.0f)) *
        Mat4x4::Scale(Vec3(box_size.x, box_size.y, 1.0f));

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->engine->pipelines.green_gui_weapon_selector_box_left.pipeline);

    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                           &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

    float frag_push[3] = {ctx->game->current_time_sec, box_size.y / box_size.x, transparencies[i]};

    AlignedPushConsts(command, ctx->engine->pipelines.green_gui_weapon_selector_box_left.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Mat4x4), mvp.data())
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(frag_push), &frag_push);

    vkCmdDraw(command, 4, 1, 0, 0);

    ////////////////////////////////////////////////////////////////////////////
    // weapon description
    ////////////////////////////////////////////////////////////////////////////

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout,
                            0, 1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);
    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                           &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

    struct VertexPushConstant
    {
      Mat4x4 mvp;
      Vec2   character_coordinate;
      Vec2   character_size;
    } vpc;

    struct FragmentPushConstant
    {
      Vec3  color;
      float time = 0.0f;
    } fpc;

    fpc.time = ctx->game->current_time_sec;

    const char* descriptions[] = {
        "Combat knife",
        "36mm gun",
        "120mm cannon",
    };

    //--------------------------------------------------------------------------
    // tilt rulers values
    //--------------------------------------------------------------------------
    const char* selection = descriptions[i];
    const int   length    = static_cast<int>(SDL_strlen(selection));
    float       cursor    = 0.0f;

    for (int j = 0; j < length; ++j)
    {
      GenerateSdfFontCommand cmd = {
          .character             = selection[j],
          .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 250.0f,
          .position              = {translation.x - 110.0f, translation.y - 10.0f, -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      vpc.character_coordinate = r.character_coordinate;
      vpc.character_size       = r.character_size;
      vpc.mvp                  = gui_projection * r.transform;
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = ctx->engine->extent2D};
      vkCmdSetScissor(command, 0, 1, &scissor);

      fpc.color = Vec3(145.0f, 224.0f, 194.0f).scale(1.0f / 255.0f);

      AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(vpc), &vpc)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(fpc), &fpc);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
}

void weapon_selectors_right(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  if (ctx->game->player.freecam_mode)
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);

  Mat4x4 gui_projection;
  gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

  Vec2 screen_extent = {(float)ctx->engine->extent2D.width, (float)ctx->engine->extent2D.height};

  Vec2 box_size                 = {120.0f, 25.0f};
  Vec2 offset_from_bottom_right = {25.0f, 25.0f};

  float transparencies[3];
  ctx->game->level.weapon_selections[1].calculate(transparencies);

  for (int i = 0; i < 3; ++i)
  {
    ////////////////////////////////////////////////////////////////////////////
    // Bordered box for the text inside
    ////////////////////////////////////////////////////////////////////////////
    const Vec2 t = {screen_extent.x - box_size.x - offset_from_bottom_right.x - (14.0f * static_cast<float>(i)),
                    screen_extent.y - (box_size.y * 2.00f * static_cast<float>(i + 1)) - offset_from_bottom_right.y};

    const Mat4x4 mvp =
        gui_projection * Mat4x4::Translation(Vec3(t.x, t.y, -1.0f)) * Mat4x4::Scale(Vec3(box_size.x, box_size.y, 1.0f));

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->engine->pipelines.green_gui_weapon_selector_box_right.pipeline);

    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                           &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

    float frag_push[3] = {ctx->game->current_time_sec, box_size.y / box_size.x, transparencies[i]};

    AlignedPushConsts(command, ctx->engine->pipelines.green_gui_weapon_selector_box_right.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Mat4x4), mvp.data())
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(frag_push), &frag_push);

    vkCmdDraw(command, 4, 1, 0, 0);

    ////////////////////////////////////////////////////////////////////////////
    // weapon description
    ////////////////////////////////////////////////////////////////////////////

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.green_gui_sdf_font.layout,
                            0, 1, &ctx->game->materials.lucida_sans_sdf_dset, 0, nullptr);
    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                           &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

    struct VertexPushConstant
    {
      Mat4x4 mvp;
      Vec2   character_coordinate;
      Vec2   character_size;
    } vpc;

    struct FragmentPushConstant
    {
      Vec3  color;
      float time = 0.0f;
    } fpc;

    fpc.time = ctx->game->current_time_sec;

    const char* descriptions[] = {
        "Combat knife",
        "36mm gun",
        "120mm cannon",
    };

    //--------------------------------------------------------------------------
    // tilt rulers values
    //--------------------------------------------------------------------------
    const char* selection = descriptions[i];
    const int   length    = static_cast<int>(SDL_strlen(selection));
    float       cursor    = 0.0f;

    for (int j = 0; j < length; ++j)
    {
      GenerateSdfFontCommand cmd = {
          .character             = selection[j],
          .lookup_table          = ctx->game->materials.lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->materials.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->materials.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 250.0f,
          .position              = {t.x - 105.0f - 30.0f * (0.4f - transparencies[i]), t.y - 10.0f, -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      vpc.character_coordinate = r.character_coordinate;
      vpc.character_size       = r.character_size;
      vpc.mvp                  = gui_projection * r.transform;
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = ctx->engine->extent2D};
      vkCmdSetScissor(command, 0, 1, &scissor);

      fpc.color = Vec3(145.0f, 224.0f, 194.0f).scale(1.0f / 255.0f);

      AlignedPushConsts(command, ctx->engine->pipelines.green_gui_sdf_font.layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, vpc)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, fpc);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
}

#if 0
    void hello_world_text(ThreadJobData tjd)
{
  VkCommandBuffer    command = acquire_command_buffer(tjd);
  SimpleRenderingCmd result  = {.command = command, .subpass = Engine::SimpleRendering::Pass::RobotGui};
  ctx->game->simple_rendering_cmds.push(result);

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = ctx->engine->simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = ctx->engine->simple_rendering.framebuffers[ctx->game->image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(command, &begin_info);
  }

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    ctx->engine->simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      ctx->engine->simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &ctx->game->lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    mat4x4 mvp;
    vec2   character_coordinate;
    vec2   character_size;
  } vpc = {};

  struct FragmentPushConstant
  {
    vec3  color;
    float time;
  } fpc = {};

  fpc.time = ctx->game->current_time_sec;

  //--------------------------------------------------------------------------
  // 3D rotating text demo
  //--------------------------------------------------------------------------
  {
    mat4x4 gui_projection = {};

    {
      float extent_width        = static_cast<float>(ctx->engine->extent2D.width);
      float extent_height       = static_cast<float>(ctx->engine->extent2D.height);
      float aspect_ratio        = extent_width / extent_height;
      float fov                 = to_rad(90.0f);
      float near_clipping_plane = 0.001f;
      float far_clipping_plane  = 100.0f;
      mat4x4_perspective(gui_projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
      gui_projection[1][1] *= -1.0f;
    }

    mat4x4 gui_view = {};

    {
      vec3 center   = {0.0f, 0.0f, 0.0f};
      vec3 up       = {0.0f, -1.0f, 0.0f};
      vec3 position = {0.0f, 0.0f, -10.0f};
      mat4x4_look_at(gui_view, position, center, up);
    }

    mat4x4 projection_view = {};
    mat4x4_mul(projection_view, gui_projection, gui_view);

    float      cursor = 0.0f;
    const char word[] = "Hello World!";

    for (const char c : word)
    {
      if ('\0' == c)
        continue;

      GenerateSdfFontCommand cmd = {
          .character             = c,
          .lookup_table          = ctx->game->lucida_sans_sdf_char_ids,
          .character_data        = ctx->game->lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(ctx->game->lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 30.0f,
          .position              = {2.0f, 6.0f, 0.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, projection_view, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = ctx->engine->extent2D};
      vkCmdSetScissor(command, 0, 1, &scissor);

      vkCmdPushConstants(
          command, ctx->engine->simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 1.0f;
      fpc.color[2] = 1.0f;

      vkCmdPushConstants(
          command, ctx->engine->simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(command);
}
#endif

void imgui(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  const ImDrawData* draw_data   = ImGui::GetDrawData();
  const size_t      vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  const size_t      index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if ((0 == vertex_size) or (0 == index_size))
    return;

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command, 5));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  ctx->engine->insert_debug_marker(command, "imgui", {1.0f, 0.0f, 0.0f, 1.0f});

  if (vertex_size and index_size)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.imgui.pipeline);

    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.imgui.layout, 0, 1,
                            &ctx->game->materials.imgui_font_atlas_dset, 0, nullptr);

    vkCmdBindIndexBuffer(command, ctx->engine->gpu_host_coherent_memory_buffer,
                         ctx->game->materials.imgui_index_buffer_offsets[ctx->game->image_index], VK_INDEX_TYPE_UINT16);

    vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_host_coherent_memory_buffer,
                           &ctx->game->materials.imgui_vertex_buffer_offsets[ctx->game->image_index]);

    ImGuiIO& io = ImGui::GetIO();

    {
      VkViewport viewport = {
          .width    = io.DisplaySize.x,
          .height   = io.DisplaySize.y,
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };
      vkCmdSetViewport(command, 0, 1, &viewport);
    }

    float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    float translate[] = {-1.0f, -1.0f};

    AlignedPushConsts(command, ctx->engine->pipelines.imgui.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(scale), scale)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(translate), translate);

    {
      int32_t  vtx_offset = 0;
      uint32_t idx_offset = 0;

      for (int n = 0; n < draw_data->CmdListsCount; n++)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
          const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
          if (pcmd->UserCallback)
          {
            pcmd->UserCallback(cmd_list, pcmd);
          }
          else
          {
            VkRect2D scissor = {
                .offset =
                    {
                        .x = (int32_t)(pcmd->ClipRect.x) > 0 ? (int32_t)(pcmd->ClipRect.x) : 0,
                        .y = (int32_t)(pcmd->ClipRect.y) > 0 ? (int32_t)(pcmd->ClipRect.y) : 0,
                    },
                .extent =
                    {
                        .width  = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                        .height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1) // FIXME: Why +1 here?
                    },
            };
            vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdDrawIndexed(command, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
          }
          idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
      }
    }
  }

  vkEndCommandBuffer(command);
}

void water(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.pbr_water.pipeline);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.regular_billboard_vertex_buffer_offset);

  Mat4x4 rotation_matrix = Mat4x4::RotationX(to_rad(90.0f));
  Mat4x4 scale_matrix    = Mat4x4::Scale(Vec3(20.0f, 20.0f, 1.0f));

  for (int i = 0; i < 9; ++i)
  {
    struct PushConst
    {
      Mat4x4 projection;
      Mat4x4 view;
      Mat4x4 model;
      Vec3   camPos;
      float  time = 0.0f;
    } push;

    push.projection = ctx->game->player.camera_projection;
    push.view       = ctx->game->player.camera_view;

    push.model = Mat4x4::Translation(Vec3(40.0f * static_cast<float>(i % 3) - 40.0f,
                                          10.5f + 0.02f * SDL_sinf(ctx->game->current_time_sec),
                                          40.0f * static_cast<float>(i / 3) - 40.0f)) *
                 rotation_matrix * scale_matrix;

    push.camPos = ctx->game->player.get_camera().position;
    push.time   = ctx->game->current_time_sec;

    vkCmdPushConstants(command, ctx->engine->pipelines.pbr_water.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

    VkDescriptorSet dsets[] = {ctx->game->materials.pbr_ibl_environment_dset,
                               ctx->game->materials.pbr_dynamic_lights_dset,
                               ctx->game->materials.pbr_water_material_dset};

    uint32_t dynamic_offsets[] = {
        static_cast<uint32_t>(ctx->game->materials.pbr_dynamic_lights_ubo_offsets[ctx->game->image_index])};

    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.pbr_water.layout, 0,
                            array_size(dsets), dsets, array_size(dynamic_offsets), dynamic_offsets);

    vkCmdDraw(command, 4, 1, 0, 0);
  }
  vkEndCommandBuffer(command);
}

[[maybe_unused]] void debug_shadowmap(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->gui_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.gui.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.debug_billboard.pipeline);

  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_device_local_memory_buffer,
                         &ctx->game->materials.green_gui_billboard_vertex_buffer_offset);

  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.debug_billboard.layout, 0, 1,
                          &ctx->game->materials.debug_shadow_map_dset, 0, nullptr);

  Mat4x4 gui_projection;
  gui_projection.ortho(0, ctx->engine->extent2D.width, 0, ctx->engine->extent2D.height, 0.0f, 1.0f);

  for (uint32_t cascade = 0; cascade < SHADOWMAP_CASCADE_COUNT; ++cascade)
  {
    const float rectangle_dimension_pixels = 120.0f;
    Vec2        translation                = {rectangle_dimension_pixels + 10.0f, rectangle_dimension_pixels + 220.0f};

    switch (cascade)
    {
    case 0:
      break;
    case 1:
      translation.x += (2.1f * rectangle_dimension_pixels);
      break;
    case 2:
      translation.y += (2.1f * rectangle_dimension_pixels);
      break;
    case 3:
      translation.x += (2.1f * rectangle_dimension_pixels);
      translation.y += (2.1f * rectangle_dimension_pixels);
      break;
    default:
      break;
    }

    const Mat4x4 mvp = gui_projection * Mat4x4::Translation(Vec3(translation.x, translation.y, -1.0f)) *
                       Mat4x4::Scale(Vec3(rectangle_dimension_pixels, rectangle_dimension_pixels, 1.0f));

    AlignedPushConsts(command, ctx->engine->pipelines.debug_billboard.layout)
        .push(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Mat4x4), mvp.data())
        .push(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(cascade), &cascade);

    vkCmdDraw(command, 4, 1, 0, 0);
  }

  vkEndCommandBuffer(command);
}

[[maybe_unused]] void orientation_axis(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.colored_geometry.pipeline);

  RenderEntityParams params(ctx->game->player);
  params.cmd             = command;
  params.pipeline_layout = ctx->engine->pipelines.colored_geometry.layout;

  const Vec3 colors[] = {
      Vec3(1.0f, 0.0f, 0.0f),
      Vec3(0.0f, 1.0f, 0.0f),
      Vec3(0.0f, 0.0f, 1.0f),
  };

  for (uint32_t i = 0; i < SDL_arraysize(ctx->game->level.axis_arrow_entities); ++i)
  {
    params.color = colors[i];
    render_entity(ctx->game->level.axis_arrow_entities[i], ctx->game->materials.lil_arrow, *ctx->engine, params);
  }

  vkEndCommandBuffer(command);
}

void tesselated_ground(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  VkCommandBuffer command = acquire_command_buffer(tjd);
  ctx->game->scene_rendering_commands.push(PrioritizedCommandBuffer(command));
  ctx->engine->render_passes.color_and_depth.begin(command, ctx->game->image_index);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.tesselated_ground.pipeline);
  vkCmdBindVertexBuffers(command, 0, 1, &ctx->engine->gpu_host_coherent_memory_buffer,
                         &ctx->game->materials.tesselation_vb_offset);

  const VkShaderStageFlags stages = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  AlignedPushConsts(command, ctx->engine->pipelines.tesselated_ground.layout)
      .push(stages, ctx->game->player.camera_projection)
      .push(stages, ctx->game->player.camera_view)
      .push(stages, ctx->game->player.get_camera().position)
      .push(stages, ctx->game->DEBUG_VEC2.x)
      .push(stages, ctx->game->current_time_sec);

  const Materials& mats    = ctx->game->materials;
  VkDescriptorSet  dsets[] = {
      mats.frustum_planes_dset[ctx->game->image_index],
      mats.sandy_level_pbr_material_dset,
      mats.pbr_ibl_environment_dset,
      mats.debug_shadow_map_dset,
      mats.pbr_dynamic_lights_dset,
      mats.cascade_view_proj_matrices_render_dset[ctx->game->image_index],
  };

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(ctx->game->materials.pbr_dynamic_lights_ubo_offsets[ctx->game->image_index])};

  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->engine->pipelines.tesselated_ground.layout, 0,
                          array_size(dsets), dsets, array_size(dynamic_offsets), dynamic_offsets);

  vkCmdSetLineWidth(command, 2.0f);
  vkCmdDraw(command, ctx->game->materials.tesselation_instances, 1, 0, 0);

  vkEndCommandBuffer(command);
}

void update_memory_host_coherent_ubo(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  Engine& e = *ctx->engine;
  Game&   g = *ctx->game;

  //
  // Cascade shadow map projection matrices
  //
  {
    MemoryMap csm(e.device, e.memory_blocks.host_coherent_ubo.memory,
                  g.materials.cascade_view_proj_mat_ubo_offsets[g.image_index],
                  SHADOWMAP_CASCADE_COUNT * (sizeof(Mat4x4) + sizeof(float)));

    std::copy(g.materials.cascade_split_depths, &g.materials.cascade_split_depths[SHADOWMAP_CASCADE_COUNT],
              reinterpret_cast<float*>(std::copy(g.materials.cascade_view_proj_mat,
                                                 &g.materials.cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT],
                                                 reinterpret_cast<Mat4x4*>(*csm))));
  }

  //
  // light sources
  //
  {
    MemoryMap light_sources(e.device, e.memory_blocks.host_coherent_ubo.memory,
                            g.materials.pbr_dynamic_lights_ubo_offsets[g.image_index], sizeof(LightSourcesSoA));
    *reinterpret_cast<LightSourcesSoA*>(*light_sources) = g.materials.pbr_light_sources_cache;
  }

  //
  // rigged simple skinning matrices
  //
  {
    const uint32_t count = g.materials.riggedSimple.skins[0].joints.count;
    const uint32_t size  = count * sizeof(Mat4x4);
    const Mat4x4*  begin = g.level.rigged_simple_entity.joint_matrices;
    const Mat4x4*  end   = &begin[count];

    MemoryMap joint_matrices(e.device, e.memory_blocks.host_coherent_ubo.memory,
                             g.materials.rig_skinning_matrices_ubo_offsets[g.image_index], size);
    std::copy(begin, end, reinterpret_cast<Mat4x4*>(*joint_matrices));
  }

  //
  // monster skinning matrices
  //
  {
    const uint32_t count = g.materials.monster.skins[0].joints.count;
    const uint32_t size  = count * sizeof(Mat4x4);
    const Mat4x4*  begin = g.level.monster_entity.joint_matrices;
    const Mat4x4*  end   = &begin[count];

    MemoryMap joint_matrices(e.device, e.memory_blocks.host_coherent_ubo.memory,
                             g.materials.monster_skinning_matrices_ubo_offsets[g.image_index], size);
    std::copy(begin, end, reinterpret_cast<Mat4x4*>(*joint_matrices));
  }

  //
  // frustum planes
  //
  {
    MemoryMap frustums(e.device, e.memory_blocks.host_coherent_ubo.memory,
                       g.materials.frustum_planes_ubo_offsets[g.image_index], 6 * sizeof(Vec4));
    (g.player.camera_projection * g.player.camera_view).generate_frustum_planes(reinterpret_cast<Vec4*>(*frustums));
  }
}

void update_memory_host_coherent(ThreadJobData tjd)
{
  JobContext*     ctx = reinterpret_cast<JobContext*>(tjd.user_data);
  ScopedPerfEvent perf_event(ctx->game->render_profiler, __FUNCTION__, tjd.thread_id);

  {
    MemoryMap map(ctx->engine->device, ctx->engine->memory_blocks.host_coherent.memory,
                  ctx->game->materials.green_gui_rulers_buffer_offsets[ctx->game->image_index],
                  MAX_ROBOT_GUI_LINES * sizeof(Vec2));

#if 0
    std::copy(ctx->game->materials.gui_lines_memory_cache,
              ctx->game->materials.gui_lines_memory_cache + MAX_ROBOT_GUI_LINES, reinterpret_cast<Vec2*>(*map));
#else
    const LinesRenderer& r = ctx->game->level.lines_renderer;
    std::copy(r.position_cache, r.position_cache + r.position_cache_size, reinterpret_cast<Vec2*>(*map));
#endif
  }

  DebugGui::render(*ctx->engine, *ctx->game);
}

} // namespace

Job* ExampleLevel::copy_render_jobs(Job* dst)
{
  const Job jobs[] = {
      update_memory_host_coherent_ubo,
      update_memory_host_coherent,
      radar,
      robot_gui_lines,
      height_ruler_text,
      tilt_ruler_text,
      robot_gui_speed_meter_text,
      robot_gui_speed_meter_triangle,
      compass_text,
      radar_dots,
      weapon_selectors_left,
      weapon_selectors_right,
      skybox_job,
      tesselated_ground,
      robot_job,
      helmet_job,
      point_light_boxes,
      matrioshka_box,
      water,
      simple_rigged,
      monster_rigged,
      robot_depth_job,
      helmet_depth_job,
      imgui,
  };
  return std::copy(jobs, jobs + array_size(jobs), dst);
}
