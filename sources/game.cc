#include "game.hh"
#include "engine/cascade_shadow_mapping.hh"
#include "engine/cubemap.hh"
#include "engine/memory_map.hh"
#include "engine/merge_sort.hh"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <algorithm>

void Game::startup(Engine& engine)
{
  debug_gui.setup();
  materials.setup(engine);
  materials.light_source_position = Vec3(0.0f, -1.0f, 1.0f);
  player.setup(engine.extent2D.width, engine.extent2D.height);
  level.setup(*engine.generic_allocator, materials);

  SDL_SetRelativeMouseMode(SDL_FALSE);

  update_profiler.paused = false;
  render_profiler.paused = false;

  update_profiler.skip_frames = 5;
  render_profiler.skip_frames = 5;

  story.setup(*engine.generic_allocator);

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
  level.teardown(*engine.generic_allocator);
  debug_gui.teardown();
  materials.teardown(engine);
  vkDeviceWaitIdle(engine.device);
}

void Game::update(Engine& engine, float time_delta_since_last_frame_ms)
{
  update_profiler.on_frame();
  render_profiler.on_frame();

  {
    bool      quit_requested = false;
    SDL_Event event          = {};
    while (SDL_PollEvent(&event))
    {
      debug_gui.process_event(*this, event);
      if (!debug_gui.engine_console_open)
      {
        player.process_event(event);
        level.process_event(event);
      }

      switch (event.type)
      {
      case SDL_MOUSEBUTTONDOWN: {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = true;
          SDL_GetMouseState(&lmb_last_cursor_position[0], &lmb_last_cursor_position[1]);
          lmb_current_cursor_position[0] = lmb_last_cursor_position[0];
          lmb_current_cursor_position[1] = lmb_last_cursor_position[1];
        }
      }
      break;

      case SDL_MOUSEMOTION: {
        if (lmb_clicked)
        {
          SDL_GetMouseState(&lmb_current_cursor_position[0], &lmb_current_cursor_position[1]);
        }
      }
      break;

      case SDL_MOUSEBUTTONUP: {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = false;
        }
      }
      break;

      case SDL_KEYDOWN:
      case SDL_KEYUP: {
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

  ScopedPerfEvent perf_event(update_profiler, __PRETTY_FUNCTION__, 0);

  {
    ScopedPerfEvent recording_perf(update_profiler, "debug_gui_update", 1);
    debug_gui.update(engine, *this);
  }

  {
    ScopedPerfEvent recording_perf(update_profiler, "player_update", 1);
    player.update(current_time_sec, time_delta_since_last_frame_ms, level);
  }

  {
    ScopedPerfEvent recording_perf(update_profiler, "level_update", 1);
    level.update(time_delta_since_last_frame_ms);
  }

  materials.pbr_light_sources_cache.count = 0;

  engine.job_system.fill_jobs(ExampleLevel::copy_update_jobs);
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

    engine.job_system.fill_jobs(ExampleLevel::copy_render_jobs);
    engine.job_system.start();
    // @todo: do something useful here as well?
    engine.job_system.wait_for_finish();

    {
      ScopedPerfEvent recording_perf(render_profiler, "record_primary_command_buffer", 1);
      record_primary_command_buffer(engine);
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

namespace {

void execute_commands(Engine& engine, VkCommandBuffer cmd, PrioritizedCommandBufferList& list)
{
  const uint32_t list_size = list.size();

  if (0 != list_size)
  {
    {
      PrioritizedCommandBuffer* tmp = engine.generic_allocator->allocate<PrioritizedCommandBuffer>(list_size);
      merge_sort(list.begin(), list.begin() + list_size, tmp);
      engine.generic_allocator->free(tmp, list_size);
    }

    auto             extract_command_buffer = [](const PrioritizedCommandBuffer& it) { return it.data; };
    VkCommandBuffer* command_buffers        = engine.generic_allocator->allocate<VkCommandBuffer>(list_size);
    VkCommandBuffer* end =
        std::transform(list.begin(), list.begin() + list_size, command_buffers, extract_command_buffer);

    vkCmdExecuteCommands(cmd, std::distance(command_buffers, end), command_buffers);

    engine.generic_allocator->free(command_buffers, list_size);
  }
}

} // namespace

void Game::record_primary_command_buffer(Engine& engine)
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
    {
      ScopedPerfEvent recording_perf(render_profiler, "depth_pass_begin_render_pass", 2);
      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    }
    for (const ShadowmapCommandBuffer& iter : shadow_mapping_pass_commands)
      if (cascade_idx == iter.cascade_idx)
        vkCmdExecuteCommands(cmd, 1, &iter.cmd);
    {
      ScopedPerfEvent recording_perf(render_profiler, "depth_pass_end_render_pass", 2);
      vkCmdEndRenderPass(cmd);
    }
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

    {
      ScopedPerfEvent recording_perf(render_profiler, "skybox_begin_render_pass", 2);
      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    }

    if(VK_NULL_HANDLE != skybox_command)
    {
      vkCmdExecuteCommands(cmd, 1, &skybox_command);
    }

    {
      ScopedPerfEvent recording_perf(render_profiler, "skybox_end_render_pass", 2);
      vkCmdEndRenderPass(cmd);
    }
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

    {
      ScopedPerfEvent recording_perf(render_profiler, "scene_pass_begin_render_pass", 2);
      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    }
    execute_commands(engine, cmd, scene_rendering_commands);
    {
      ScopedPerfEvent recording_perf(render_profiler, "scene_pass_end_render_pass", 2);
      vkCmdEndRenderPass(cmd);
    }
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

    {
      ScopedPerfEvent recording_perf(render_profiler, "gui_pass_begin_render_pass", 2);
      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    }

    execute_commands(engine, cmd, gui_commands);

    {
      ScopedPerfEvent recording_perf(render_profiler, "gui_pass_end_render_pass", 2);
      vkCmdEndRenderPass(cmd);
    }
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

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(cmd);
}
