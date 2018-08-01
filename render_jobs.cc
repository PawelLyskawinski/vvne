#include "render_jobs.hh"

// game_render_entity.cc
void render_pbr_entity(Entity entity, EntityComponentSystem& ecs, gltf::RenderableModel& model, Engine& engine,
                       RenderEntityParams& p);
void render_entity(Entity entity, EntityComponentSystem& ecs, gltf::RenderableModel& model, Engine& engine,
                   RenderEntityParams& p);

// game_generate_gui_lines.cc
void generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count);
void generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count);

// game_generate_sdf_font.cc
GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd);

namespace {

uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

float pixels_to_line_length(int pixels, int pixels_max_size)
{
  return static_cast<float>(2 * pixels) / static_cast<float>(pixels_max_size);
}

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

constexpr float to_deg(float rad) noexcept
{
  return (180.0f * rad) / float(M_PI);
}

} // namespace

namespace render {

int skybox_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Skybox;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Skybox,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  struct
  {
    mat4x4 projection;
    mat4x4 view;
  } push = {};

  mat4x4_dup(push.projection, tjd.game.projection);
  mat4x4_dup(push.view, tjd.game.view);

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Skybox]);

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox], 0, 1,
                          &tjd.game.skybox_cubemap_dset, 0, nullptr);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

  const Node& node = tjd.game.box.scene_graph.nodes.data[1];
  Mesh&       mesh = tjd.game.box.scene_graph.meshes.data[node.mesh];

  vkCmdBindIndexBuffer(tjd.command, tjd.engine.gpu_static_geometry.buffer, mesh.indices_offset, mesh.indices_type);
  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer, &mesh.vertices_offset);
  vkCmdDrawIndexed(tjd.command, mesh.indices_count, 1, 0, 0, 0);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int robot_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  {
    VkDescriptorSet dsets[]    = {tjd.game.pbr_ibl_environment_dset, tjd.game.pbr_dynamic_lights_dset};
    uint32_t dynamic_offsets[] = {static_cast<uint32_t>(tjd.game.pbr_dynamic_lights_ubo_offsets[tjd.game.image_index])};

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 1,
                            SDL_arraysize(dsets), dsets, SDL_arraysize(dynamic_offsets), dynamic_offsets);
  }

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0,
                          1, &tjd.game.robot_pbr_material_dset, 0, nullptr);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 0.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::Scene3D,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_pbr_entity(tjd.game.robot_entity, tjd.game.ecs, tjd.game.robot, tjd.engine, params);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int helmet_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  {
    VkDescriptorSet dsets[]    = {tjd.game.pbr_ibl_environment_dset, tjd.game.pbr_dynamic_lights_dset};
    uint32_t dynamic_offsets[] = {static_cast<uint32_t>(tjd.game.pbr_dynamic_lights_ubo_offsets[tjd.game.image_index])};

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 1,
                            SDL_arraysize(dsets), dsets, SDL_arraysize(dynamic_offsets), dynamic_offsets);
  }

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0,
                          1, &tjd.game.helmet_pbr_material_dset, 0, nullptr);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 0.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::Scene3D,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_pbr_entity(tjd.game.helmet_entity, tjd.game.ecs, tjd.game.helmet, tjd.engine, params);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int point_light_boxes(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 0.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::ColoredGeometry,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));

  for (unsigned i = 0; i < SDL_arraysize(tjd.game.box_entities); ++i)
  {
    SDL_memcpy(params.color, tjd.game.pbr_light_sources_cache.colors[i], sizeof(vec3));
    render_entity(tjd.game.box_entities[i], tjd.game.ecs, tjd.game.box, tjd.engine, params);
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int matrioshka_box(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 1.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::ColoredGeometry,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_entity(tjd.game.matrioshka_entity, tjd.game.ecs, tjd.game.animatedBox, tjd.engine, params);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int vr_scene(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, tjd.game.projection, tjd.game.view);

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, 0.0, 2.5, 0.0);

  mat4x4 rotation_matrix = {};
  mat4x4_identity(rotation_matrix);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, VR_LEVEL_SCALE, VR_LEVEL_SCALE, VR_LEVEL_SCALE);

  mat4x4 tmp = {};
  mat4x4_mul(tmp, translation_matrix, rotation_matrix);

  mat4x4 model = {};
  mat4x4_mul(model, tmp, scale_matrix);

  mat4x4 mvp = {};
  mat4x4_mul(mvp, projection_view, model);

  vkCmdBindIndexBuffer(tjd.command, tjd.engine.gpu_static_geometry.buffer, tjd.game.vr_level_index_buffer_offset,
                       tjd.game.vr_level_index_type);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.vr_level_vertex_buffer_offset);

  vec3 color = {0.5, 0.5, 1.0};
  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

  vkCmdDrawIndexed(tjd.command, static_cast<uint32_t>(tjd.game.vr_level_index_count), 1, 0, 0, 0);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int simple_rigged(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(tjd.game.rig_skinning_matrices_ubo_offsets[tjd.game.image_index])};

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0, 1,
      &tjd.game.rig_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 0.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_entity(tjd.game.rigged_simple_entity, tjd.game.ecs, tjd.game.riggedSimple, tjd.engine, params);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int monster_rigged(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(tjd.game.monster_skinning_matrices_ubo_offsets[tjd.game.image_index])};

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0, 1,
      &tjd.game.monster_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {1.0f, 1.0f, 1.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_entity(tjd.game.monster_entity, tjd.game.ecs, tjd.game.monster, tjd.engine, params);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int radar(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGui]);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

  mat4x4 gui_projection = {};
  mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
               tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

  const float rectangle_dimension_pixels = 100.0f;
  const float offset_from_edge           = 10.0f;

  const vec2 translation = {rectangle_dimension_pixels + offset_from_edge,
                            rectangle_dimension_pixels + offset_from_edge};

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, translation[0], translation[1], -1.0f);

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, rectangle_dimension_pixels, rectangle_dimension_pixels, 1.0f);

  mat4x4 world_transform = {};
  mat4x4_mul(world_transform, translation_matrix, scale_matrix);

  mat4x4 mvp = {};
  mat4x4_mul(mvp, gui_projection, world_transform);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(float), &tjd.game.current_time_sec);

  vkCmdDraw(tjd.command, 4, 1, 0, 0);
  vkEndCommandBuffer(tjd.command);
  return 0;
}

int robot_gui_lines(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiLines]);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_host_visible.buffer,
                         &tjd.game.green_gui_rulers_buffer_offsets[tjd.game.image_index]);

  uint32_t offset = 0;

  // ------ GREEN ------
  {
    VkRect2D scissor{.extent = tjd.engine.generic_handles.extent2D};
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_green_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {125.0f / 255.0f, 204.0f / 255.0f, 174.0f / 255.0f, 0.9f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ RED ------
  {
    VkRect2D scissor{};
    scissor.extent.width  = line_to_pixel_length(1.50f, tjd.engine.generic_handles.extent2D.width);
    scissor.extent.height = line_to_pixel_length(1.02f, tjd.engine.generic_handles.extent2D.height);
    scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = line_to_pixel_length(0.29f, tjd.engine.generic_handles.extent2D.height); // 118
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_red_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {1.0f, 0.0f, 0.0f, 0.9f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ YELLOW ------
  {
    VkRect2D scissor      = {};
    scissor.extent.width  = line_to_pixel_length(0.5f, tjd.engine.generic_handles.extent2D.width);
    scissor.extent.height = line_to_pixel_length(1.3f, tjd.engine.generic_handles.extent2D.height);
    scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = line_to_pixel_length(0.2f, tjd.engine.generic_handles.extent2D.height);
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_yellow_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {1.0f, 1.0f, 0.0f, 0.7f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int robot_gui_speed_meter_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

  fpc.time = tjd.game.current_time_sec;

  {
    mat4x4 gui_projection = {};
    mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
                 tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

    float speed     = vec3_len(tjd.game.player_velocity) * 1500.0f;
    int   speed_int = static_cast<int>(speed);

    char thousands = 0;
    while (1000 <= speed_int)
    {
      thousands += 1;
      speed_int -= 1000;
    }

    char hundreds = 0;
    while (100 <= speed_int)
    {
      hundreds += 1;
      speed_int -= 100;
    }

    char tens = 0;
    while (10 <= speed_int)
    {
      tens += 1;
      speed_int -= 10;
    }

    char singles = 0;
    while (1 <= speed_int)
    {
      singles += 1;
      speed_int -= 1;
    }

    char text_form[4] = {};

    text_form[0] = thousands + '0';
    text_form[1] = hundreds + '0';
    text_form[2] = tens + '0';
    text_form[3] = singles + '0';

    float cursor = 0.0f;

    for (const char c : text_form)
    {
      auto line_to_pixel_length = [](float coord, int pixel_max_size) -> float {
        return (coord * pixel_max_size * 0.5f);
      };

      GenerateSdfFontCommand cmd = {
          .character             = c,
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 220.0f, // tjd.game.DEBUG_VEC2[0],
          .position =
              {
                  line_to_pixel_length(0.48f,
                                       tjd.engine.generic_handles.extent2D.width), // 0.65f
                  line_to_pixel_length(0.80f,
                                       tjd.engine.generic_handles.extent2D.height), // 0.42f
                  -1.0f,
              },
          .cursor = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 125.0f / 255.0f;
      fpc.color[1] = 204.0f / 255.0f;
      fpc.color[2] = 174.0f / 255.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int robot_gui_speed_meter_triangle(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiTriangle]);

  struct VertPush
  {
    vec4 offset;
    vec4 scale;
  } vpush = {
      .offset = {-0.384f, -0.180f, 0.0f, 0.0f},
      .scale  = {0.012f, 0.02f, 1.0f, 1.0f},
  };

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiTriangle],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpush), &vpush);

  vec4 color = {125.0f / 255.0f, 204.0f / 255.0f, 174.0f / 255.0f, 1.0f};

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiTriangle],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpush), sizeof(vec4), color);

  vkCmdDraw(tjd.command, 3, 1, 0, 0);
  vkEndCommandBuffer(tjd.command);
  return 0;
}

int height_ruler_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // height rulers values
  //--------------------------------------------------------------------------
  ArrayView<GuiHeightRulerText> scheduled_text_data = {};

  {
    GenerateGuiLinesCommand cmd = {
        .player_y_location_meters = -(2.0f - tjd.game.player_position[1]),
        .camera_x_pitch_radians   = tjd.game.camera_angle,
        .camera_y_pitch_radians   = tjd.game.camera_updown_angle,
        .screen_extent2D          = tjd.engine.generic_handles.extent2D,
    };

    generate_gui_height_ruler_text(cmd, nullptr, &scheduled_text_data.count);
    scheduled_text_data.data = tjd.allocator.allocate<GuiHeightRulerText>(scheduled_text_data.count);
    generate_gui_height_ruler_text(cmd, scheduled_text_data.data, &scheduled_text_data.count);
  }

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    mat4x4 gui_projection = {};
    mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
                 tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset[0], text.offset[1], -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = line_to_pixel_length(0.75f, tjd.engine.generic_handles.extent2D.width);
      scissor.extent.height = line_to_pixel_length(1.02f, tjd.engine.generic_handles.extent2D.height);
      scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = line_to_pixel_length(0.29f, tjd.engine.generic_handles.extent2D.height); // 118
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 0.0f;
      fpc.color[2] = 0.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int tilt_ruler_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // tilt rulers values
  //--------------------------------------------------------------------------
  ArrayView<GuiHeightRulerText> scheduled_text_data = {};

  {
    GenerateGuiLinesCommand cmd = {
        .player_y_location_meters = -(2.0f - tjd.game.player_position[1]),
        .camera_x_pitch_radians   = tjd.game.camera_angle,
        .camera_y_pitch_radians   = tjd.game.camera_updown_angle,
        .screen_extent2D          = tjd.engine.generic_handles.extent2D,
    };

    generate_gui_tilt_ruler_text(cmd, nullptr, &scheduled_text_data.count);
    scheduled_text_data.data = tjd.allocator.allocate<GuiHeightRulerText>(scheduled_text_data.count);
    generate_gui_tilt_ruler_text(cmd, scheduled_text_data.data, &scheduled_text_data.count);
  }

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    mat4x4 gui_projection = {};
    mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
                 tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset[0], text.offset[1], -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = line_to_pixel_length(0.5f, tjd.engine.generic_handles.extent2D.width);
      scissor.extent.height = line_to_pixel_length(1.3f, tjd.engine.generic_handles.extent2D.height);
      scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = line_to_pixel_length(0.2f, tjd.engine.generic_handles.extent2D.height);
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 1.0f;
      fpc.color[2] = 0.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int compass_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

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
  } fpc = {.time = tjd.game.current_time_sec};

  const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

  const float direction_increment = to_rad(22.5f);

  float angle_mod = tjd.game.camera_angle + (0.5f * direction_increment);
  if (angle_mod > (2 * M_PI))
    angle_mod -= (2 * M_PI);

  int direction_iter = 0;
  while (angle_mod > direction_increment)
  {
    direction_iter += 1;
    angle_mod -= direction_increment;
  }

  const int left_direction_iter  = (0 == direction_iter) ? (SDL_arraysize(directions) - 1) : (direction_iter - 1);
  const int right_direction_iter = ((SDL_arraysize(directions) - 1) == direction_iter) ? 0 : direction_iter + 1;

  const char* center_text = directions[direction_iter];
  const char* left_text   = directions[left_direction_iter];
  const char* right_text  = directions[right_direction_iter];

  mat4x4 gui_projection = {};
  mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
               tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);
  float cursor = 0.0f;

  //////////////////////////////////////////////////////////////////////////////
  // CENTER TEXT RENDERING
  //////////////////////////////////////////////////////////////////////////////
  for (unsigned i = 0; i < SDL_strlen(center_text); ++i)
  {
    const char c = center_text[i];

    if ('\0' == c)
      continue;

    auto line_to_pixel_length = [](float coord, int pixel_max_size) -> float {
      return (coord * pixel_max_size * 0.5f);
    };

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
        .character_data        = tjd.game.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
        .texture_size          = {512, 256},
        .scaling               = 300.0f,
        .position =
            {
                line_to_pixel_length(1.0f - angle_mod + (0.5f * direction_increment),
                                     tjd.engine.generic_handles.extent2D.width),
                line_to_pixel_length(1.335f, tjd.engine.generic_handles.extent2D.height),
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
    SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
    mat4x4_mul(vpc.mvp, gui_projection, r.transform);
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

    fpc.color[0] = 125.0f / 255.0f;
    fpc.color[1] = 204.0f / 255.0f;
    fpc.color[2] = 174.0f / 255.0f;

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

    vkCmdDraw(tjd.command, 4, 1, 0, 0);
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

    auto line_to_pixel_length = [](float coord, int pixel_max_size) -> float {
      return (coord * pixel_max_size * 0.5f);
    };

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
        .character_data        = tjd.game.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
        .texture_size          = {512, 256},
        .scaling               = 200.0f, // tjd.game.DEBUG_VEC2[0],
        .position =
            {
                line_to_pixel_length(0.8f, tjd.engine.generic_handles.extent2D.width),    // 0.65f
                line_to_pixel_length(1.345f, tjd.engine.generic_handles.extent2D.height), // 0.42f
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
    SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
    mat4x4_mul(vpc.mvp, gui_projection, r.transform);
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

    fpc.color[0] = 125.0f / 255.0f;
    fpc.color[1] = 204.0f / 255.0f;
    fpc.color[2] = 174.0f / 255.0f;

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

    vkCmdDraw(tjd.command, 4, 1, 0, 0);
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

    auto line_to_pixel_length = [](float coord, int pixel_max_size) -> float {
      return (coord * pixel_max_size * 0.5f);
    };

    GenerateSdfFontCommand cmd = {
        .character             = c,
        .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
        .character_data        = tjd.game.lucida_sans_sdf_chars,
        .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
        .texture_size          = {512, 256},
        .scaling               = 200.0f, // tjd.game.DEBUG_VEC2[0],
        .position =
            {
                line_to_pixel_length(1.2f, tjd.engine.generic_handles.extent2D.width),    // 0.65f
                line_to_pixel_length(1.345f, tjd.engine.generic_handles.extent2D.height), // 0.42f
                -1.0f,
            },
        .cursor = cursor,
    };

    GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

    SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
    SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
    mat4x4_mul(vpc.mvp, gui_projection, r.transform);
    cursor += r.cursor_movement;

    VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

    fpc.color[0] = 125.0f / 255.0f;
    fpc.color[1] = 204.0f / 255.0f;
    fpc.color[2] = 174.0f / 255.0f;

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

    vkCmdDraw(tjd.command, 4, 1, 0, 0);
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int radar_dots(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RadarDots;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RadarDots,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots]);

  int   rectangle_dim           = 100;
  float vertical_length         = pixels_to_line_length(rectangle_dim, tjd.engine.generic_handles.extent2D.width);
  float offset_from_screen_edge = pixels_to_line_length(rectangle_dim / 10, tjd.engine.generic_handles.extent2D.width);

  const float horizontal_length = pixels_to_line_length(rectangle_dim, tjd.engine.generic_handles.extent2D.height);
  const float offset_from_top_edge =
      pixels_to_line_length(rectangle_dim / 10, tjd.engine.generic_handles.extent2D.height);

  const vec2 center_radar_position = {
      -1.0f + offset_from_screen_edge + vertical_length,
      -1.0f + offset_from_top_edge + horizontal_length,
  };

  vec2 robot_position  = {tjd.game.vr_level_goal[0], tjd.game.vr_level_goal[1]};
  vec2 player_position = {tjd.game.player_position[0], tjd.game.player_position[2]};

  // players position becomes the cartesian (0, 0) point for us, hence the substraction order
  vec2 distance = {};
  vec2_sub(distance, robot_position, player_position);

  // normalization helps to
  vec2 normalized = {};
  vec2_norm(normalized, distance);

  float robot_angle = SDL_atan2f(normalized[0], normalized[1]);
  float angle       = tjd.game.camera_angle - robot_angle - ((float)M_PI / 2.0f);

  float final_distance = 0.01f * vec2_len(distance);

  float aspect_ratio = vertical_length / horizontal_length;

  const vec2 helmet_position = {aspect_ratio * final_distance * SDL_sinf(angle), final_distance * SDL_cosf(angle)};

  vec2 relative_helmet_position = {};
  vec2_sub(relative_helmet_position, center_radar_position, helmet_position);

  vec4 position = {relative_helmet_position[0], relative_helmet_position[1], 0.0f, 1.0f};
  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vec4), position);

  vec4 color = {1.0f, 0.0f, 0.0f, (final_distance < 0.22f) ? 0.6f : 0.0f};
  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vec4), sizeof(vec4), color);

  vkCmdDraw(tjd.command, 1, 1, 0, 0);
  vkEndCommandBuffer(tjd.command);
  return 0;
}

int weapon_selectors_left(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  mat4x4 gui_projection = {};
  mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
               tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

  vec2 screen_extent = {(float)tjd.engine.generic_handles.extent2D.width,
                        (float)tjd.engine.generic_handles.extent2D.height};

  vec2 box_size                = {120.0f, 25.0f};
  vec2 offset_from_bottom_left = {25.0f, 25.0f};

  float transparencies[3];
  tjd.game.weapon_selections[0].calculate(transparencies);

  for (int i = 0; i < 3; ++i)
  {
    ////////////////////////////////////////////////////////////////////////////
    // Bordered box for the text inside
    ////////////////////////////////////////////////////////////////////////////
    vec2 translation = {box_size[0] + offset_from_bottom_left[0] + (14.0f * i),
                        screen_extent[1] - (box_size[1] * 2.00f * (i + 1)) - offset_from_bottom_left[1]};

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, translation[0], translation[1], -1.0f);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, box_size[0], box_size[1], 1.0f);

    mat4x4 world_transform = {};
    mat4x4_mul(world_transform, translation_matrix, scale_matrix);

    mat4x4 mvp = {};
    mat4x4_mul(mvp, gui_projection, world_transform);

    vkCmdBindPipeline(
        tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft]);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                           &tjd.game.green_gui_billboard_vertex_buffer_offset);

    vkCmdPushConstants(
        tjd.command,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft],
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

    float frag_push[] = {tjd.game.current_time_sec, box_size[1] / box_size[0], transparencies[i]};
    vkCmdPushConstants(
        tjd.command,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft],
        VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(frag_push), &frag_push);

    vkCmdDraw(tjd.command, 4, 1, 0, 0);

    ////////////////////////////////////////////////////////////////////////////
    // weapon description
    ////////////////////////////////////////////////////////////////////////////

    vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

    vkCmdBindDescriptorSets(
        tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
        &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                           &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

    fpc.time = tjd.game.current_time_sec;

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
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 250.0f,
          .position              = {translation[0] - 110.0f, translation[1] - 10.0f, -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 145.0f / 255.0f;
      fpc.color[1] = 224.0f / 255.0f;
      fpc.color[2] = 194.0f / 255.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int weapon_selectors_right(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  mat4x4 gui_projection = {};
  mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
               tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

  vec2 screen_extent = {(float)tjd.engine.generic_handles.extent2D.width,
                        (float)tjd.engine.generic_handles.extent2D.height};

  vec2 box_size                 = {120.0f, 25.0f};
  vec2 offset_from_bottom_right = {25.0f, 25.0f};

  float transparencies[3];
  tjd.game.weapon_selections[1].calculate(transparencies);

  for (int i = 0; i < 3; ++i)
  {
    ////////////////////////////////////////////////////////////////////////////
    // Bordered box for the text inside
    ////////////////////////////////////////////////////////////////////////////
    vec2 translation = {screen_extent[0] - box_size[0] - offset_from_bottom_right[0] - (14.0f * i),
                        screen_extent[1] - (box_size[1] * 2.00f * (i + 1)) - offset_from_bottom_right[1]};

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, translation[0], translation[1], -1.0f);

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, box_size[0], box_size[1], 1.0f);

    mat4x4 world_transform = {};
    mat4x4_mul(world_transform, translation_matrix, scale_matrix);

    mat4x4 mvp = {};
    mat4x4_mul(mvp, gui_projection, world_transform);

    vkCmdBindPipeline(
        tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight]);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                           &tjd.game.green_gui_billboard_vertex_buffer_offset);

    vkCmdPushConstants(
        tjd.command,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight],
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

    float frag_push[] = {tjd.game.current_time_sec, box_size[1] / box_size[0], transparencies[i]};
    vkCmdPushConstants(
        tjd.command,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight],
        VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(frag_push), &frag_push);

    vkCmdDraw(tjd.command, 4, 1, 0, 0);

    ////////////////////////////////////////////////////////////////////////////
    // weapon description
    ////////////////////////////////////////////////////////////////////////////

    vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

    vkCmdBindDescriptorSets(
        tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
        &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                           &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

    fpc.time = tjd.game.current_time_sec;

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
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 250.0f,
          .position = {translation[0] - 105.0f - 30.0f * (0.4f - transparencies[i]), translation[1] - 10.0f, -1.0f},
          .cursor   = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 145.0f / 255.0f;
      fpc.color[1] = 224.0f / 255.0f;
      fpc.color[2] = 194.0f / 255.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int hello_world_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::RobotGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::RobotGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

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

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // 3D rotating text demo
  //--------------------------------------------------------------------------
  {
    mat4x4 gui_projection = {};

    {
      float extent_width        = static_cast<float>(tjd.engine.generic_handles.extent2D.width);
      float extent_height       = static_cast<float>(tjd.engine.generic_handles.extent2D.height);
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
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
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

      VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 1.0f;
      fpc.color[2] = 1.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int imgui(ThreadJobData tjd)
{
  ImDrawData* draw_data = ImGui::GetDrawData();
  ImGuiIO&    io        = ImGui::GetIO();

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if ((0 == vertex_size) or (0 == index_size))
    return -1;

  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  if (vertex_size and index_size)
  {
    vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ImGui]);

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui], 0,
                            1, &tjd.game.imgui_font_atlas_dset, 0, nullptr);

    vkCmdBindIndexBuffer(tjd.command, tjd.engine.gpu_host_visible.buffer,
                         tjd.game.debug_gui.index_buffer_offsets[tjd.game.image_index], VK_INDEX_TYPE_UINT16);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_host_visible.buffer,
                           &tjd.game.debug_gui.vertex_buffer_offsets[tjd.game.image_index]);

    {
      VkViewport viewport = {
          .width    = io.DisplaySize.x,
          .height   = io.DisplaySize.y,
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };
      vkCmdSetViewport(tjd.command, 0, 1, &viewport);
    }

    float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    float translate[] = {-1.0f, -1.0f};

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, scale);
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

    {
      int vtx_offset = 0;
      int idx_offset = 0;

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
            vkCmdSetScissor(tjd.command, 0, 1, &scissor);
            vkCmdDrawIndexed(tjd.command, pcmd->ElemCount, 1, static_cast<uint32_t>(idx_offset), vtx_offset, 0);
          }
          idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
      }
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

} // namespace render
