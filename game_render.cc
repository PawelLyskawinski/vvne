#include "engine.hh"
#include "game.hh"
#include <SDL2/SDL_assert.h>
#include <linmath.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

} // namespace

void game_render(Game& game, Engine& engine, float current_time_sec)
{
  Engine::SimpleRenderer& renderer = engine.simple_renderer;

  uint32_t image_index = 0;
  vkAcquireNextImageKHR(engine.device, engine.swapchain, UINT64_MAX, engine.image_available, VK_NULL_HANDLE,
                        &image_index);
  vkWaitForFences(engine.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.device, 1, &renderer.submition_fences[image_index]);

  // ----------------------------------------------------------
  //                          CUBES
  // ----------------------------------------------------------
  {
    VkCommandBuffer cmd = renderer.scene.secondary_command_buffers[image_index];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = 0;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipelines[0]);
    vkCmdBindIndexBuffer(cmd, renderer.scene.cube_buffer, offsetof(CubeBuffer, indices), VK_INDEX_TYPE_UINT32);
    {
      VkDeviceSize offset = offsetof(CubeBuffer, vertices);
      vkCmdBindVertexBuffers(cmd, 0, 1, &renderer.scene.cube_buffer, &offset);
    }

    // cube 1
    {
      {
        const int max_handled_textures         = SDL_arraysize(renderer.descriptor_sets) / SWAPCHAIN_IMAGES_COUNT;
        const int offset_to_usable_descriptors = max_handled_textures * image_index;
        const int descriptor_idx               = offset_to_usable_descriptors + 1;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[0], 0, 1,
                                &renderer.descriptor_sets[descriptor_idx], 0, nullptr);
      }

      mat4x4 view   = {};
      vec3   eye    = {6.0f, 6.7f, 30.0f};
      vec3   center = {-3.0f, 0.0f, -1.0f};
      vec3   up     = {0.0f, 1.0f, 0.0f};
      mat4x4_look_at(view, eye, center, up);

      mat4x4 projection          = {};
      float  extent_width        = static_cast<float>(engine.extent2D.width);
      float  extent_height       = static_cast<float>(engine.extent2D.height);
      float  aspect_ratio        = extent_width / extent_height;
      float  fov                 = 100.0f;
      float  near_clipping_plane = 0.001f;
      float  far_clipping_plane  = 10000.0f;
      mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

      mat4x4 projectionview = {};
      mat4x4_mul(projectionview, projection, view);

      mat4x4 model = {};
      mat4x4_identity(model);
      mat4x4_translate(model, 0.0f, 0.0f, -4.0f);
      mat4x4_rotate_Y(model, model, current_time_sec);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projectionview, model);

      vkCmdPushConstants(cmd, renderer.pipeline_layouts[0], VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    }

    // cube 2
    {
      {
        const int max_handled_textures         = SDL_arraysize(renderer.descriptor_sets) / SWAPCHAIN_IMAGES_COUNT;
        const int offset_to_usable_descriptors = max_handled_textures * image_index;
        const int descriptor_idx               = offset_to_usable_descriptors;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[0], 0, 1,
                                &renderer.descriptor_sets[descriptor_idx], 0, nullptr);
      }

      mat4x4 view   = {};
      vec3   eye    = {6.0f, 6.7f, 30.0f};
      vec3   center = {-3.0f, 0.0f, -1.0f};
      vec3   up     = {0.0f, 1.0f, 0.0f};
      mat4x4_look_at(view, eye, center, up);

      mat4x4 projection          = {};
      float  extent_width        = static_cast<float>(engine.extent2D.width);
      float  extent_height       = static_cast<float>(engine.extent2D.height);
      float  aspect_ratio        = extent_width / extent_height;
      float  fov                 = 100.0f;
      float  near_clipping_plane = 0.001f;
      float  far_clipping_plane  = 10000.0f;
      mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

      mat4x4 projectionview = {};
      mat4x4_mul(projectionview, projection, view);

      mat4x4 model = {};
      mat4x4_identity(model);
      mat4x4_translate(model, 2.0f, 3.0f, -6.0f);
      mat4x4_rotate_Y(model, model, current_time_sec);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projectionview, model);

      vkCmdPushConstants(cmd, renderer.pipeline_layouts[0], VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    }



    // helmet
    {
      {
        const int max_handled_textures         = SDL_arraysize(renderer.descriptor_sets) / SWAPCHAIN_IMAGES_COUNT;
        const int offset_to_usable_descriptors = max_handled_textures * image_index;
        const int descriptor_idx               = offset_to_usable_descriptors + game.renderableHelmet.albedo_texture_idx;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[0], 0, 1,
                                &renderer.descriptor_sets[descriptor_idx], 0, nullptr);
      }

      vkCmdBindIndexBuffer(cmd, game.renderableHelmet.device_buffer, game.renderableHelmet.indices_offset,
                           game.renderableHelmet.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &game.renderableHelmet.device_buffer, &game.renderableHelmet.vertices_offset);

      mat4x4 view   = {};
      vec3   eye    = {6.0f, 6.7f, 30.0f};
      vec3   center = {-3.0f, 0.0f, -1.0f};
      vec3   up     = {0.0f, 1.0f, 0.0f};
      mat4x4_look_at(view, eye, center, up);

      mat4x4 projection          = {};
      float  extent_width        = static_cast<float>(engine.extent2D.width);
      float  extent_height       = static_cast<float>(engine.extent2D.height);
      float  aspect_ratio        = extent_width / extent_height;
      float  fov                 = 100.0f;
      float  near_clipping_plane = 0.001f;
      float  far_clipping_plane  = 10000.0f;
      mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

      mat4x4 projectionview = {};
      mat4x4_mul(projectionview, projection, view);

      mat4x4 model = {};
      mat4x4_identity(model);

      //mat4x4_translate(model, -5.0f, 2.0f, 10.0f);
      mat4x4_translate(model, game.helmet_translation[0], game.helmet_translation[1], game.helmet_translation[2]);
      mat4x4_rotate_Y(model, model, current_time_sec * 0.3f);
      mat4x4_rotate_X(model, model, to_rad(90.0));
      mat4x4_scale_aniso(model, model, 1.6f, 1.6f, 1.6f);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projectionview, model);

      vkCmdPushConstants(cmd, renderer.pipeline_layouts[0], VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      vkCmdDrawIndexed(cmd, game.renderableHelmet.indices_count, 1, 0, 0, 0);
    }

    vkEndCommandBuffer(cmd);
  }

  // ----------------------------------------------------------
  //                          IMGUI
  // ----------------------------------------------------------
  {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGuiIO&    io        = ImGui::GetIO();

    size_t vertex_size            = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t max_vertex_buffer_size = 10000;
    SDL_assert(max_vertex_buffer_size >= vertex_size);

    if (VK_NULL_HANDLE == renderer.gui.vertex_buffers[image_index])
    {
      {
        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = max_vertex_buffer_size;
        ci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(engine.device, &ci, nullptr, &renderer.gui.vertex_buffers[image_index]);
      }

      {
        VkMemoryRequirements reqs = {};
        vkGetBufferMemoryRequirements(engine.device, renderer.gui.vertex_buffers[image_index], &reqs);

        VkPhysicalDeviceMemoryProperties properties = {};
        vkGetPhysicalDeviceMemoryProperties(engine.physical_device, &properties);

        VkMemoryAllocateInfo allocate{};
        allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize  = reqs.size;
        allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        vkAllocateMemory(engine.device, &allocate, nullptr, &renderer.gui.vertex_memory[image_index]);
        vkBindBufferMemory(engine.device, renderer.gui.vertex_buffers[image_index],
                           renderer.gui.vertex_memory[image_index], 0);
      }
    }

    size_t index_size            = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
    size_t max_index_buffer_size = 10000;
    SDL_assert(max_index_buffer_size >= index_size);

    if (VK_NULL_HANDLE == renderer.gui.index_buffers[image_index])
    {
      {
        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = max_index_buffer_size;
        ci.usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(engine.device, &ci, nullptr, &renderer.gui.index_buffers[image_index]);
      }

      {
        VkMemoryRequirements reqs = {};
        vkGetBufferMemoryRequirements(engine.device, renderer.gui.index_buffers[image_index], &reqs);

        VkPhysicalDeviceMemoryProperties properties = {};
        vkGetPhysicalDeviceMemoryProperties(engine.physical_device, &properties);

        VkMemoryAllocateInfo allocate{};
        allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize  = reqs.size;
        allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        vkAllocateMemory(engine.device, &allocate, nullptr, &renderer.gui.index_memory[image_index]);
        vkBindBufferMemory(engine.device, renderer.gui.index_buffers[image_index],
                           renderer.gui.index_memory[image_index], 0);
      }
    }

    // Upload vertex and index data
    {
      ImDrawVert* vtx_dst = nullptr;
      ImDrawIdx*  idx_dst = nullptr;

      vkMapMemory(engine.device, renderer.gui.vertex_memory[image_index], 0, max_vertex_buffer_size, 0,
                  (void**)(&vtx_dst));
      vkMapMemory(engine.device, renderer.gui.index_memory[image_index], 0, max_index_buffer_size, 0,
                  (void**)(&idx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
      }

      VkMappedMemoryRange vertex_range = {};
      vertex_range.sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      vertex_range.memory              = renderer.gui.vertex_memory[image_index];
      vertex_range.size                = VK_WHOLE_SIZE;

      VkMappedMemoryRange index_range = {};
      index_range.sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      index_range.memory              = renderer.gui.index_memory[image_index];
      index_range.size                = VK_WHOLE_SIZE;

      VkMappedMemoryRange ranges[] = {vertex_range, index_range};
      vkFlushMappedMemoryRanges(engine.device, SDL_arraysize(ranges), ranges);

      vkUnmapMemory(engine.device, renderer.gui.vertex_memory[image_index]);
      vkUnmapMemory(engine.device, renderer.gui.index_memory[image_index]);
    }

    VkCommandBuffer command_buffer = renderer.gui.secondary_command_buffers[image_index];
    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = 1;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(command_buffer, &begin);
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipelines[1]);

    {
      const int max_handled_textures         = SDL_arraysize(renderer.descriptor_sets) / SWAPCHAIN_IMAGES_COUNT;
      const int offset_to_usable_descriptors = max_handled_textures * image_index;
      const int descriptor_idx               = offset_to_usable_descriptors + 2;
      vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[1], 0, 1,
                              &renderer.descriptor_sets[descriptor_idx], 0, nullptr);
    }

    vkCmdBindIndexBuffer(command_buffer, renderer.gui.index_buffers[image_index], 0, VK_INDEX_TYPE_UINT16);
    {
      VkDeviceSize vertex_offset = {0};
      vkCmdBindVertexBuffers(command_buffer, 0, 1, &renderer.gui.vertex_buffers[image_index], &vertex_offset);
    }

    {
      VkViewport viewport{};
      viewport.width    = io.DisplaySize.x;
      viewport.height   = io.DisplaySize.y;
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    }

    float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    float translate[] = {-1.0f, -1.0f};

    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[1], VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2,
                       scale);
    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[1], VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2,
                       sizeof(float) * 2, translate);

    {
      ImDrawData* draw_data = ImGui::GetDrawData();

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
            {
              VkRect2D scissor{};
              scissor.offset.x      = (int32_t)(pcmd->ClipRect.x) > 0 ? (int32_t)(pcmd->ClipRect.x) : 0;
              scissor.offset.y      = (int32_t)(pcmd->ClipRect.y) > 0 ? (int32_t)(pcmd->ClipRect.y) : 0;
              scissor.extent.width  = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
              scissor.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // FIXME: Why +1 here?
              vkCmdSetScissor(command_buffer, 0, 1, &scissor);
            }
            vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
          }
          idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
      }
    }

    vkEndCommandBuffer(command_buffer);
  }

  // ----------------------------------------------------------
  // SUBMISSION
  // ----------------------------------------------------------
  {
    VkCommandBuffer cmd = renderer.primary_command_buffers[image_index];

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      vkBeginCommandBuffer(cmd, &begin);
    }

    VkClearValue color_clear = {};
    {
      float clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
      SDL_memcpy(color_clear.color.float32, clear, sizeof(clear));
    }

    VkClearValue depth_clear{};
    depth_clear.depthStencil.depth = 1.0;

    VkClearValue clear_values[] = {color_clear, depth_clear};

    {
      VkRenderPassBeginInfo begin{};
      begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      begin.renderPass        = renderer.render_pass;
      begin.framebuffer       = renderer.framebuffers[image_index];
      begin.clearValueCount   = SDL_arraysize(clear_values);
      begin.pClearValues      = clear_values;
      begin.renderArea.extent = engine.extent2D;
      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    }

    vkCmdExecuteCommands(cmd, 1, &renderer.scene.secondary_command_buffers[image_index]);
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    vkCmdExecuteCommands(cmd, 1, &renderer.gui.secondary_command_buffers[image_index]);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    {
      VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo         submit{};
      submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.waitSemaphoreCount   = 1;
      submit.pWaitSemaphores      = &engine.image_available;
      submit.pWaitDstStageMask    = &wait_stage;
      submit.commandBufferCount   = 1;
      submit.pCommandBuffers      = &cmd;
      submit.signalSemaphoreCount = 1;
      submit.pSignalSemaphores    = &engine.render_finished;
      vkQueueSubmit(engine.graphics_queue, 1, &submit, renderer.submition_fences[image_index]);
    }

    {
      VkPresentInfoKHR present{};
      present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      present.waitSemaphoreCount = 1;
      present.pWaitSemaphores    = &engine.render_finished;
      present.swapchainCount     = 1;
      present.pSwapchains        = &engine.swapchain;
      present.pImageIndices      = &image_index;
      vkQueuePresentKHR(engine.graphics_queue, &present);
    }
  }
}
