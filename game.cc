#include "game.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_timer.h>
#include <linmath.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

} // namespace

void Game::startup(Engine& engine)
{
  {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    unsigned char* guifont_pixels = nullptr;
    int            guifont_w      = 0;
    int            guifont_h      = 0;
    io.Fonts->GetTexDataAsRGBA32(&guifont_pixels, &guifont_w, &guifont_h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(guifont_pixels, guifont_w, guifont_h, 32, 4 * guifont_w,
                                                              SDL_PIXELFORMAT_RGBA8888);
    debug_gui.font_texture_idx = engine.load_texture(surface);
    SDL_FreeSurface(surface);

    struct Mapping
    {
      ImGuiKey_    imgui;
      SDL_Scancode sdl;
    } mappings[] = {
        // --------------------------------------------------------------------
        {ImGuiKey_Tab, SDL_SCANCODE_TAB},
        {ImGuiKey_LeftArrow, SDL_SCANCODE_LEFT},
        {ImGuiKey_RightArrow, SDL_SCANCODE_RIGHT},
        {ImGuiKey_UpArrow, SDL_SCANCODE_UP},
        {ImGuiKey_DownArrow, SDL_SCANCODE_DOWN},
        {ImGuiKey_PageUp, SDL_SCANCODE_PAGEUP},
        {ImGuiKey_PageDown, SDL_SCANCODE_PAGEDOWN},
        {ImGuiKey_Home, SDL_SCANCODE_HOME},
        {ImGuiKey_End, SDL_SCANCODE_END},
        {ImGuiKey_Insert, SDL_SCANCODE_INSERT},
        {ImGuiKey_Delete, SDL_SCANCODE_DELETE},
        {ImGuiKey_Backspace, SDL_SCANCODE_BACKSPACE},
        {ImGuiKey_Space, SDL_SCANCODE_SPACE},
        {ImGuiKey_Enter, SDL_SCANCODE_RETURN},
        {ImGuiKey_Escape, SDL_SCANCODE_ESCAPE},
        {ImGuiKey_A, SDL_SCANCODE_A},
        {ImGuiKey_C, SDL_SCANCODE_C},
        {ImGuiKey_V, SDL_SCANCODE_V},
        {ImGuiKey_X, SDL_SCANCODE_X},
        {ImGuiKey_Y, SDL_SCANCODE_Y},
        {ImGuiKey_Z, SDL_SCANCODE_Z}
        // --------------------------------------------------------------------
    };

    for (Mapping mapping : mappings)
      io.KeyMap[mapping.imgui] = mapping.sdl;

    io.RenderDrawListsFn  = nullptr;
    io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
    io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
    io.ClipboardUserData  = nullptr;

    struct CursorMapping
    {
      ImGuiMouseCursor_ imgui;
      SDL_SystemCursor  sdl;
    } cursor_mappings[] = {
        // --------------------------------------------------------------------
        {ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW},
        {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM},
        {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL},
        {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS},
        {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE},
        {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW},
        {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE}
        // --------------------------------------------------------------------
    };

    for (CursorMapping mapping : cursor_mappings)
      debug_gui.mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
  }

  {
    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      debug_gui.vertex_buffer_offsets[i] = engine.gpu_host_visible.allocate(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES);
      debug_gui.index_buffer_offsets[i]  = engine.gpu_host_visible.allocate(DebugGui::INDEX_BUFFER_CAPACITY_BYTES);
    }
  }

  {
    const int memorySize = 2000; // adjusted manually
    helmet.memory        = engine.double_ended_stack.allocate_back<uint8_t>(memorySize);
    SDL_memset(helmet.memory, 0, memorySize);

    helmet.loadASCII(engine.double_ended_stack, "../assets/DamagedHelmet/glTF/DamagedHelmet.gltf");
    SDL_Log("helmet used %d / %d bytes", helmet.usedMemory, memorySize);
    // helmet.debugDump();
    renderableHelmet.construct(engine, helmet);
    engine.double_ended_stack.reset_back();
  }

  // skybox
  {
    const int memorySize = 500; // adjusted manually
    box.memory           = engine.double_ended_stack.allocate_back<uint8_t>(memorySize);
    SDL_memset(box.memory, 0, memorySize);

    box.loadASCII(engine.double_ended_stack, "../assets/Box.gltf");
    SDL_Log("box used %d / %d bytes", box.usedMemory, memorySize);
    // box.debugDump();
    renderableBox.construct(engine, box);
    engine.double_ended_stack.reset_back();
  }

  environment_hdr_map_idx                 = engine.load_texture("../assets/old_industrial_hall.hdr");
  environment_equirectangular_texture_idx = engine.load_texture("../assets/old_industrial_hall.jpg");

  //
  // IMGUI descriptor sets
  //
  {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool     = engine.generic_handles.descriptor_pool;
    allocate.descriptorSetCount = SDL_arraysize(imgui_descriptor_sets);
    allocate.pSetLayouts        = engine.simple_rendering.descriptor_set_layouts;

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, imgui_descriptor_sets);
  }

  for (int image_index = 0; image_index < SWAPCHAIN_IMAGES_COUNT; ++image_index)
  {
    VkDescriptorImageInfo image{};
    image.sampler     = engine.generic_handles.texture_samplers[image_index];
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine.images.image_views[debug_gui.font_texture_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 1;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = imgui_descriptor_sets[image_index];

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  //
  // HELMET descriptor sets
  //
  {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool     = engine.generic_handles.descriptor_pool;
    allocate.descriptorSetCount = SDL_arraysize(helmet_descriptor_sets);
    allocate.pSetLayouts        = engine.simple_rendering.descriptor_set_layouts;

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, helmet_descriptor_sets);
  }

  for (int image_index = 0; image_index < SWAPCHAIN_IMAGES_COUNT; ++image_index)
  {
    VkDescriptorImageInfo image{};
    image.sampler     = engine.generic_handles.texture_samplers[image_index];
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine.images.image_views[renderableHelmet.albedo_texture_idx];
    // image.imageView = engine.images.image_views[environment_equirectangular_texture_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 1;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = helmet_descriptor_sets[image_index];

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  //
  // SKYBOX descriptor sets
  //
  {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool     = engine.generic_handles.descriptor_pool;
    allocate.descriptorSetCount = SDL_arraysize(skybox_descriptor_sets);
    allocate.pSetLayouts        = engine.simple_rendering.descriptor_set_layouts;

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, skybox_descriptor_sets);
  }

  for (int image_index = 0; image_index < SWAPCHAIN_IMAGES_COUNT; ++image_index)
  {
    VkDescriptorImageInfo image{};
    image.sampler     = engine.generic_handles.texture_samplers[image_index];
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine.images.image_views[environment_equirectangular_texture_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 1;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = skybox_descriptor_sets[image_index];

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  helmet_translation[0] = 2.2f;
  helmet_translation[1] = 3.5f;
  helmet_translation[2] = 19.2f;
}

void Game::teardown(Engine&)
{
  for (SDL_Cursor* cursor : debug_gui.mousecursors)
    SDL_FreeCursor(cursor);
}

void Game::update(Engine& engine, float current_time_sec)
{
  (void)current_time_sec;
  uint64_t start_function_ticks = SDL_GetPerformanceCounter();

  ImGuiIO& io = ImGui::GetIO();

  // Event dispatching
  {
    SDL_Event event = {};
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
      case SDL_MOUSEWHEEL:
      {
        bool scroll_up   = event.wheel.y > 0.0;
        bool scroll_down = event.wheel.y < 0.0;

        if (scroll_up)
        {
          io.MouseWheel = 1.0f;
        }
        else if (scroll_down)
        {
          io.MouseWheel = -1.0f;
        }
      }
      break;

      case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(event.text.text);
        break;

      case SDL_MOUSEBUTTONDOWN:
      {
        switch (event.button.button)
        {
        case SDL_BUTTON_LEFT:
          debug_gui.mousepressed[0] = true;
          break;
        case SDL_BUTTON_RIGHT:
          debug_gui.mousepressed[1] = true;
          break;
        case SDL_BUTTON_MIDDLE:
          debug_gui.mousepressed[2] = true;
          break;
        default:
          break;
        }
      }
      break;

      case SDL_KEYDOWN:
      case SDL_KEYUP:
      {
        io.KeysDown[event.key.keysym.scancode] = (SDL_KEYDOWN == event.type);

        io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
        io.KeyCtrl  = ((SDL_GetModState() & KMOD_CTRL) != 0);
        io.KeyAlt   = ((SDL_GetModState() & KMOD_ALT) != 0);
        io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
      }
      break;

      default:
        break;
      }
    }
  }

  {
    SDL_Window* window = engine.generic_handles.window;
    int         w, h;
    SDL_GetWindowSize(window, &w, &h);

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    int    mx, my;
    Uint32 mouseMask               = SDL_GetMouseState(&mx, &my);
    bool   is_mouse_in_window_area = 0 < (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS);
    io.MousePos = is_mouse_in_window_area ? ImVec2((float)mx, (float)my) : ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = debug_gui.mousepressed[0] || (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    io.MouseDown[1] = debug_gui.mousepressed[1] || (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    io.MouseDown[2] = debug_gui.mousepressed[2] || (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;

    for (bool& iter : debug_gui.mousepressed)
      iter = false;

    if ((SDL_GetWindowFlags(window) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MOUSE_CAPTURE)) != 0)
      io.MousePos              = ImVec2((float)mx, (float)my);
    bool any_mouse_button_down = false;
    for (int n = 0; n < IM_ARRAYSIZE(io.MouseDown); n++)
      any_mouse_button_down |= io.MouseDown[n];
    if (any_mouse_button_down && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE) == 0)
      SDL_CaptureMouse(SDL_TRUE);
    if (!any_mouse_button_down && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE) != 0)
      SDL_CaptureMouse(SDL_FALSE);

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || ImGuiMouseCursor_None == cursor)
    {
      SDL_ShowCursor(0);
    }
    else
    {
      SDL_SetCursor(debug_gui.mousecursors[cursor] ? debug_gui.mousecursors[cursor]
                                                   : debug_gui.mousecursors[ImGuiMouseCursor_Arrow]);
      SDL_ShowCursor(1);
    }

    SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);
  }

  ImGui::NewFrame();
  ImGui::PlotHistogram("update times", update_times, SDL_arraysize(update_times), 0, nullptr, 0.0, 0.001,
                       ImVec2(300, 20));
  ImGui::PlotHistogram("render times", render_times, SDL_arraysize(render_times), 0, nullptr, 0.0, 0.03,
                       ImVec2(300, 20));

  float avg_time = 0.0f;
  for (float time : update_times)
    avg_time += time;
  avg_time /= SDL_arraysize(update_times);

  ImGui::Text("Average update time: %f", avg_time);

  for (float time : render_times)
    avg_time += time;
  avg_time /= SDL_arraysize(render_times);

  ImGui::Text("Average render time: %f", avg_time);

  uint64_t end_function_ticks = SDL_GetPerformanceCounter();
  uint64_t ticks_elapsed      = end_function_ticks - start_function_ticks;

  for (int i = 0; i < (static_cast<int>(SDL_arraysize(update_times)) - 1); ++i)
  {
    update_times[i] = update_times[i + 1];
  }

  int last_element           = SDL_arraysize(update_times) - 1;
  update_times[last_element] = (float)ticks_elapsed / (float)SDL_GetPerformanceFrequency();
}

void Game::render(Engine& engine, float current_time_sec)
{
  uint64_t                 start_function_ticks = SDL_GetPerformanceCounter();
  Engine::SimpleRendering& renderer             = engine.simple_rendering;

  uint32_t image_index = 0;
  vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                        engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
  vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::Skybox];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::Skybox;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::Skybox]);
    vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, renderableBox.indices_offset,
                         renderableBox.indices_type);
    vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &renderableBox.vertices_offset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Skybox], 0, 1,
                            &skybox_descriptor_sets[image_index], 0, nullptr);

    mat4x4 view{};
    vec3   eye    = {6.0f, 6.7f, 30.0f};
    vec3   center = {-3.0f, 0.0f, -1.0f};
    vec3   up     = {0.0f, -1.0f, 0.0f};
    mat4x4_look_at(view, eye, center, up);

    mat4x4 projection{};
    float  extent_width        = static_cast<float>(engine.generic_handles.extent2D.width);
    float  extent_height       = static_cast<float>(engine.generic_handles.extent2D.height);
    float  aspect_ratio        = extent_width / extent_height;
    float  fov                 = 100.0f;
    float  near_clipping_plane = 0.001f;
    float  far_clipping_plane  = 10000.0f;
    mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

    mat4x4 model{};
    mat4x4_identity(model);
    mat4x4_rotate_Y(model, model, current_time_sec * 0.1f);

    {
      const float scale = 150.0f;
      mat4x4_scale_aniso(model, model, scale, scale, scale);
    }

    mat4x4 projectionview{};
    mat4x4_mul(projectionview, projection, view);

    mat4x4 mvp = {};
    mat4x4_mul(mvp, projectionview, model);

    vkCmdPushConstants(cmd, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Skybox],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
    struct FragPush
    {
      float exposure = 1.0f;
      float gamma    = 1.0f;
    } fragpush;
    vkCmdPushConstants(cmd, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Skybox],
                       VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), 2 * sizeof(float), &fragpush);

    vkCmdDrawIndexed(cmd, renderableBox.indices_count, 1, 0, 0, 0);

    vkEndCommandBuffer(cmd);
  }

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::Scene3D];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::Scene3D;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::Scene3D]);
    vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, renderableHelmet.indices_offset,
                         renderableHelmet.indices_type);
    vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &renderableHelmet.vertices_offset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Scene3D], 0, 1,
                            &helmet_descriptor_sets[image_index], 0, nullptr);

    mat4x4 view{};
    vec3   eye    = {6.0f, 6.7f, 30.0f};
    vec3   center = {-3.0f, 0.0f, -1.0f};
    vec3   up     = {0.0f, 1.0f, 0.0f};
    mat4x4_look_at(view, eye, center, up);

    mat4x4 projection{};
    float  extent_width        = static_cast<float>(engine.generic_handles.extent2D.width);
    float  extent_height       = static_cast<float>(engine.generic_handles.extent2D.height);
    float  aspect_ratio        = extent_width / extent_height;
    float  fov                 = 100.0f;
    float  near_clipping_plane = 0.001f;
    float  far_clipping_plane  = 10000.0f;
    mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

    mat4x4 projectionview{};
    mat4x4_mul(projectionview, projection, view);

    mat4x4 model{};
    mat4x4_identity(model);

    // mat4x4_translate(model, -5.0f, 2.0f, 10.0f);
    mat4x4_translate(model, helmet_translation[0], helmet_translation[1], helmet_translation[2]);
    mat4x4_rotate_Y(model, model, current_time_sec * 0.3f);
    mat4x4_rotate_X(model, model, to_rad(90.0));
    mat4x4_scale_aniso(model, model, 1.6f, 1.6f, 1.6f);

    mat4x4 mvp = {};
    mat4x4_mul(mvp, projectionview, model);

    vkCmdPushConstants(cmd, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Scene3D],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
    vkCmdDrawIndexed(cmd, renderableHelmet.indices_count, 1, 0, 0, 0);

    vkEndCommandBuffer(cmd);
  }

  {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGuiIO&    io        = ImGui::GetIO();

    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    SDL_assert(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
    SDL_assert(DebugGui::INDEX_BUFFER_CAPACITY_BYTES >= index_size);

    {
      ImDrawVert* vtx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.vertex_buffer_offsets[image_index], vertex_size, 0, (void**)(&vtx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx_dst += cmd_list->VtxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    {
      ImDrawIdx* idx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.index_buffer_offsets[image_index], index_size, 0, (void**)(&idx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    VkCommandBuffer command_buffer =
        renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                           Engine::SimpleRendering::Passes::ImGui];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::ImGui;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(command_buffer, &begin);
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::ImGui]);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[1], 0, 1,
                            &imgui_descriptor_sets[image_index], 0, nullptr);

    vkCmdBindIndexBuffer(command_buffer, engine.gpu_host_visible.buffer, debug_gui.index_buffer_offsets[image_index],
                         VK_INDEX_TYPE_UINT16);

    vkCmdBindVertexBuffers(command_buffer, 0, 1, &engine.gpu_host_visible.buffer,
                           &debug_gui.vertex_buffer_offsets[image_index]);

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

    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, scale);
    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

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

  engine.submit_simple_rendering(image_index);

  uint64_t end_function_ticks = SDL_GetPerformanceCounter();
  uint64_t ticks_elapsed      = end_function_ticks - start_function_ticks;

  for (int i = 0; i < (static_cast<int>(SDL_arraysize(render_times)) - 1); ++i)
  {
    render_times[i] = render_times[i + 1];
  }

  int last_element           = SDL_arraysize(render_times) - 1;
  render_times[last_element] = (float)ticks_elapsed / (float)SDL_GetPerformanceFrequency();
}
