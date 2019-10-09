#include "game.hh"
#include "engine/cubemap.hh"
#include "engine/memory_map.hh"
#include "terrain_as_a_function.hh"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>

void Game::startup(Engine& engine)
{
  debug_gui.setup();
  materials.setup(engine);
  materials.light_source_position = Vec3(0.0f, -1.0f, 1.0f);
  player.setup(engine.extent2D.width, engine.extent2D.height);
  level.setup(engine.generic_allocator, materials);

  DEBUG_VEC2.x = 0.1f;
  DEBUG_VEC2.y = -1.0f;

  DEBUG_VEC2_ADDITIONAL.x = 0.0f;
  DEBUG_VEC2_ADDITIONAL.y = 0.0f;

  DEBUG_LIGHT_ORTHO_PARAMS[0] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[1] = 10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[2] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[3] = 10.0f;

  {
    VkCommandBufferAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = engine.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = SWAPCHAIN_IMAGES_COUNT,
    };

    vkAllocateCommandBuffers(engine.device, &info, primary_command_buffers);
  }

  job_context.engine          = &engine;
  job_context.game            = this;
  engine.job_system.user_data = &job_context;
}

void Game::teardown(Engine& engine)
{
  debug_gui.teardown();
  materials.teardown(engine);
  vkDeviceWaitIdle(engine.device);
}

// CASCADE SHADOW MAPPING --------------------------------------------------------------------------------------------
// Based on:
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp
// -------------------------------------------------------------------------------------------------------------------
static void recalculate_cascade_view_proj_matrices(Mat4x4* cascade_view_proj_mat, float* cascade_split_depths,
                                                   Mat4x4 camera_projection, Mat4x4 camera_view,
                                                   Vec3 light_source_position)
{
  constexpr float cascade_split_lambda = 0.95f;
  constexpr float near_clip            = 0.001f;
  constexpr float far_clip             = 500.0f;
  constexpr float clip_range           = far_clip - near_clip;
  constexpr float min_z                = near_clip;
  constexpr float max_z                = near_clip + clip_range;
  constexpr float range                = max_z - min_z;
  constexpr float ratio                = max_z / min_z;

  //
  // This calculates the distances between frustums. For example:
  // near:      0.1
  // far:    1000.0
  // splits: 0.013, 0.034, 0.132, 1.000
  //
  float cascade_splits[SHADOWMAP_CASCADE_COUNT] = {};
  for (uint32_t i = 0; i < SHADOWMAP_CASCADE_COUNT; i++)
  {
    const float p       = static_cast<float>(i + 1) / static_cast<float>(SHADOWMAP_CASCADE_COUNT);
    const float log     = min_z * SDL_powf(ratio, p);
    const float uniform = min_z + range * p;
    const float d       = cascade_split_lambda * (log - uniform) + uniform;
    cascade_splits[i]   = (d - near_clip) / clip_range;
  }

  float last_split_dist = 0.0;
  for (uint32_t cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; cascade_idx++)
  {
    //
    // Frustum edges overview
    //
    //         4 --- 5     Y
    //       /     / |     /\  Z
    //     0 --- 1   |     | /
    //     |     |   6     .--> X
    //     |     | /
    //     3 --- 2
    //
    Vec3 frustum_corners[] = {
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, 1.0f},
    };

    //
    // LoD change should follow main game camera and not the light projection.
    // Because of that frustums have to "come out" from viewer camera.
    //
    Mat4x4 inv_cam = (camera_projection * camera_view).invert();

    for (Vec3& in : frustum_corners)
    {
      Vec4 inv_corner = inv_cam * Vec4(in, 1.0f);
      in              = inv_corner.as_vec3().scale(1.0f / inv_corner.w);
    }

    const float split_dist = cascade_splits[cascade_idx];
    for (uint32_t i = 0; i < 4; i++)
    {
      const Vec3 dist        = frustum_corners[i + 4] - frustum_corners[i];
      frustum_corners[i + 4] = frustum_corners[i] + dist.scale(split_dist);
      frustum_corners[i] += dist.scale(last_split_dist);
    }

    Vec3 frustum_center;
    for (Vec3& frustum_corner : frustum_corners)
    {
      frustum_center += frustum_corner;
    }
    frustum_center = frustum_center.scale(1.0f / 8.0f);

    float radius = 0.0f;
    for (const Vec3& frustum_corner : frustum_corners)
    {
      const float distance = (frustum_corner - frustum_center).len();
      radius               = SDL_max(radius, distance);
    }

    Vec3 max_extents = Vec3(SDL_ceilf(radius * 16.0f) / 16.0f);
    Vec3 min_extents = max_extents.invert_signs();
    Vec3 light_dir   = light_source_position.invert_signs().normalize();

    const Mat4x4 light_view_mat =
        Mat4x4::LookAt(frustum_center - light_dir.scale(-min_extents.z), frustum_center, Vec3(0.0f, -1.0f, 0.0f));

    // todo: I don't know why the near clipping plane has to be a huge negative number! If used with 0 as in tutorials,
    //       the depth is not calculated properly.. I guess for now it'll have to be this way.

    Mat4x4 light_ortho_mat;
    light_ortho_mat.ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, -50.0f,
                          max_extents.z - min_extents.z);

    cascade_view_proj_mat[cascade_idx] = light_ortho_mat * light_view_mat;
    float cascade_split_depth          = near_clip + split_dist * clip_range;
    cascade_split_depths[cascade_idx]  = cascade_split_depth;
    last_split_dist                    = cascade_splits[cascade_idx];
  }
}

void Game::update(Engine& engine, float time_delta_since_last_frame_ms)
{
  update_profiler.on_frame();
  render_profiler.on_frame();
  ScopedPerfEvent perf_event(update_profiler, __PRETTY_FUNCTION__, 0);

  {
    bool      quit_requested = false;
    SDL_Event event          = {};
    while (SDL_PollEvent(&event))
    {
      player.process_event(event);
      debug_gui.process_event(event);
      level.process_event(event);

      switch (event.type)
      {
      case SDL_MOUSEBUTTONDOWN:
      {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = true;
          SDL_GetMouseState(&lmb_last_cursor_position[0], &lmb_last_cursor_position[1]);
          lmb_current_cursor_position[0] = lmb_last_cursor_position[0];
          lmb_current_cursor_position[1] = lmb_last_cursor_position[1];
        }
      }
      break;

      case SDL_MOUSEMOTION:
      {
        if (lmb_clicked)
        {
          SDL_GetMouseState(&lmb_current_cursor_position[0], &lmb_current_cursor_position[1]);
        }
      }
      break;

      case SDL_MOUSEBUTTONUP:
      {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = false;
        }
      }
      break;

      case SDL_KEYDOWN:
      case SDL_KEYUP:
      {
        switch (event.key.keysym.scancode)
        {
        case SDL_SCANCODE_ESCAPE:
          quit_requested = true;
          break;
        default:
          break;
        }
      }

      break;

      default:
        break;
      }
    }

    if (quit_requested)
    {
      SDL_Event quit_event = {.type = SDL_QUIT};
      SDL_PushEvent(&quit_event);
    }
  }

  debug_gui.update(engine, *this);
  player.update(current_time_sec, time_delta_since_last_frame_ms, level);
  level.update(time_delta_since_last_frame_ms);

  const float acceleration_length = 5.0f * 1000.0f * player.acceleration.len();

  //
  // engines precalculation
  //
  const Mat4x4 transform_a = Mat4x4::Translation(player.position + Vec3(0.0f, -0.4f, 0.0f)) *
                             Mat4x4::RotationY(-player.camera_angle) * Mat4x4::Translation(Vec3(0.2f, 0.0f, -0.3f));
  const Mat4x4 transform_b = Mat4x4::Translation(player.position + Vec3(0.0f, -0.4f, 0.0f)) *
                             Mat4x4::RotationY(-player.camera_angle) * Mat4x4::Translation(Vec3(0.2f, 0.0f, 0.3f));

  LightSource dynamic_lights[] = {
      {
          {SDL_sinf(current_time_sec), 0.0f, 3.0f + SDL_cosf(current_time_sec), 1.0f},
          {20.0f + (5.0f * SDL_sinf(current_time_sec + 0.4f)), 0.0f, 0.0f, 1.0f},
      },
      {
          {12.8f * SDL_cosf(current_time_sec), 0.0f, -10.0f + (8.8f * SDL_sinf(current_time_sec)), 1.0f},
          {0.0f, 20.0f, 0.0f, 1.0f},
      },
      {
          {20.8f * SDL_sinf(current_time_sec / 2.0f), 0.0f, 3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f)), 1.0f},
          {0.0f, 0.0f, 20.0f, 1.0f},
      },
      {
          {SDL_sinf(current_time_sec / 1.2f), 0.0f, 2.5f * SDL_cosf(current_time_sec / 1.2f), 1.0f},
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
    light.position.y   = level.get_height(light.position.x, light.position.z) - 1.0f;
  }

  materials.pbr_light_sources_cache_last =
      std::copy(dynamic_lights, &dynamic_lights[SDL_arraysize(dynamic_lights)], materials.pbr_light_sources_cache);

  recalculate_cascade_view_proj_matrices(materials.cascade_view_proj_mat, materials.cascade_split_depths,
                                         player.camera_projection, player.camera_view, materials.light_source_position);

  Job* jobs_begin              = engine.job_system.jobs;
  Job* jobs_end                = ExampleLevel::copy_update_jobs(jobs_begin);
  engine.job_system.jobs_count = std::distance(jobs_begin, jobs_end);

  engine.job_system.start();
  ImGui::Render();
  engine.job_system.wait_for_finish();
}

void Game::render(Engine& engine)
{
  vkAcquireNextImageKHR(engine.device, engine.swapchain, UINT64_MAX, engine.image_available, VK_NULL_HANDLE,
                        &image_index);
  vkWaitForFences(engine.device, 1, &engine.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.device, 1, &engine.submition_fences[image_index]);

  engine.job_system.reset_command_buffers(image_index);

  {
    ScopedPerfEvent perf_event(render_profiler, __PRETTY_FUNCTION__, 0);

    {
      Job* jobs_begin = engine.job_system.jobs;
      Job* jobs_end   = ExampleLevel::copy_render_jobs(jobs_begin);

      engine.job_system.jobs_count = std::distance(jobs_begin, jobs_end);
      engine.job_system.start();
    }

    // While we await for tasks to be finished by worker threads, this one will handle memory synchronization

    //
    // Cascade shadow map projection matrices
    //
    {
      MemoryMap csm(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                    materials.cascade_view_proj_mat_ubo_offsets[image_index],
                    SHADOWMAP_CASCADE_COUNT * (sizeof(Mat4x4) + sizeof(float)));

      std::copy(materials.cascade_split_depths, &materials.cascade_split_depths[SHADOWMAP_CASCADE_COUNT],
                reinterpret_cast<float*>(std::copy(materials.cascade_view_proj_mat,
                                                   &materials.cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT],
                                                   reinterpret_cast<Mat4x4*>(*csm))));
    }

    //
    // light sources
    //
    {
      MemoryMap light_sources(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                              materials.pbr_dynamic_lights_ubo_offsets[image_index], sizeof(LightSourcesSoA));
      *reinterpret_cast<LightSourcesSoA*>(*light_sources) =
          convert_light_sources(materials.pbr_light_sources_cache, materials.pbr_light_sources_cache_last);
    }

    //
    // rigged simple skinning matrices
    //
    {
      const uint32_t count = materials.riggedSimple.skins[0].joints.count;
      const uint32_t size  = count * sizeof(Mat4x4);
      const Mat4x4*  begin = reinterpret_cast<Mat4x4*>(level.rigged_simple_entity.joint_matrices);
      const Mat4x4*  end   = &begin[count];

      MemoryMap joint_matrices(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                               materials.rig_skinning_matrices_ubo_offsets[image_index], size);
      std::copy(begin, end, reinterpret_cast<Mat4x4*>(*joint_matrices));
    }

    //
    // monster skinning matrices
    //
    {
      const uint32_t count = materials.monster.skins[0].joints.count;
      const uint32_t size  = count * sizeof(Mat4x4);
      const Mat4x4*  begin = reinterpret_cast<Mat4x4*>(level.monster_entity.joint_matrices);
      const Mat4x4*  end   = &begin[count];

      MemoryMap joint_matrices(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                               materials.monster_skinning_matrices_ubo_offsets[image_index], size);
      std::copy(begin, end, reinterpret_cast<Mat4x4*>(*joint_matrices));
    }

    //
    // frustum planes
    //
    {
      MemoryMap frustums(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                         materials.frustum_planes_ubo_offsets[image_index], 6 * sizeof(Vec4));
      (player.camera_projection * player.camera_view).generate_frustum_planes(reinterpret_cast<Vec4*>(*frustums));
    }

    //
    // GUI
    //
    {
      MemoryMap map(engine.device, engine.memory_blocks.host_coherent.memory,
                    materials.green_gui_rulers_buffer_offsets[image_index], MAX_ROBOT_GUI_LINES * sizeof(Vec2));
      std::copy(materials.gui_lines_memory_cache, materials.gui_lines_memory_cache + MAX_ROBOT_GUI_LINES,
                reinterpret_cast<Vec2*>(*map));
    }
    DebugGui::render(engine, *this);
    engine.job_system.wait_for_finish();

    {
      VkCommandBuffer cmd = primary_command_buffers[image_index];

      {
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &begin);
      }

      // -----------------------------------------------------------------------------------------------
      // SHADOW MAPPING PASS
      // -----------------------------------------------------------------------------------------------
      for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
      {
        VkClearValue clear_value = {.depthStencil = {1.0, 0}};

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.shadowmap.render_pass,
            .framebuffer     = engine.render_passes.shadowmap.framebuffers[cascade_idx],
            .renderArea      = {.extent = {.width = SHADOWMAP_IMAGE_DIM, .height = SHADOWMAP_IMAGE_DIM}},
            .clearValueCount = 1,
            .pClearValues    = &clear_value,
        };
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        for (const ShadowmapCommandBuffer& iter : shadow_mapping_pass_commands)
          if (cascade_idx == iter.cascade_idx)
            vkCmdExecuteCommands(cmd, 1, &iter.cmd);

        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // SKYBOX PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 1u : 2u;
        VkClearValue   clear_values[2]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color = DEFAULT_COLOR_CLEAR;
        }
        else
        {
          clear_values[0].color = DEFAULT_COLOR_CLEAR;
          clear_values[1].color = DEFAULT_COLOR_CLEAR;
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.skybox.render_pass,
            .framebuffer     = engine.render_passes.skybox.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(cmd, 1, &skybox_command);
        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // SCENE PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 2u : 3u;
        VkClearValue   clear_values[3]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color        = DEFAULT_COLOR_CLEAR;
          clear_values[1].depthStencil = {1.0, 0};
        }
        else
        {
          clear_values[0].color        = DEFAULT_COLOR_CLEAR;
          clear_values[1].depthStencil = {1.0, 0};
          clear_values[2].color        = DEFAULT_COLOR_CLEAR;
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.color_and_depth.render_pass,
            .framebuffer     = engine.render_passes.color_and_depth.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(cmd, scene_rendering_commands.size(), scene_rendering_commands.begin());
        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // GUI PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 1u : 2u;
        VkClearValue   clear_values[2]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color = DEFAULT_COLOR_CLEAR;
        }
        else
        {
          clear_values[0].color = DEFAULT_COLOR_CLEAR;
          clear_values[1].color = DEFAULT_COLOR_CLEAR;
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.gui.render_pass,
            .framebuffer     = engine.render_passes.gui.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(cmd, gui_commands.size(), gui_commands.begin());
        vkCmdEndRenderPass(cmd);
      }

      {
        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = 0,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = engine.shadowmap_image.image,
            .subresourceRange =
                {
                    .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = SHADOWMAP_CASCADE_COUNT,
                },
        };

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);
      }

      vkEndCommandBuffer(cmd);
    }

    shadow_mapping_pass_commands.reset();
    scene_rendering_commands.reset();
    gui_commands.reset();
  }

  {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &engine.image_available,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &primary_command_buffers[image_index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &engine.render_finished,
    };

    vkQueueSubmit(engine.graphics_queue, 1, &submit, engine.submition_fences[image_index]);
  }

  VkPresentInfoKHR present = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &engine.render_finished,
      .swapchainCount     = 1,
      .pSwapchains        = &engine.swapchain,
      .pImageIndices      = &image_index,
  };

  vkQueuePresentKHR(engine.graphics_queue, &present);
}
