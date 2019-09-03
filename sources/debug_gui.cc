#include "debug_gui.hh"
#include "engine/engine.hh"
#include "engine/free_list_visualizer.hh"
#include "engine/gpu_memory_visualizer.hh"
#include "engine/memory_map.hh"
#include "game.hh"
#include "profiler_visualizer.hh"
#include <SDL2/SDL_log.h>

#include <algorithm>
#include <numeric>

void DebugGui::process_event(SDL_Event& event)
{
  ImGuiIO& io = ImGui::GetIO();

  switch (event.type)
  {
  case SDL_MOUSEWHEEL:
    if (0.0f != event.wheel.y)
    {
      io.MouseWheel = (0 > event.wheel.y) ? -1.0f : 1.0f;
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
      mousepressed[0] = true;
      break;
    case SDL_BUTTON_RIGHT:
      mousepressed[1] = true;
      break;
    case SDL_BUTTON_MIDDLE:
      mousepressed[2] = true;
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

    switch (event.key.keysym.scancode)
    {
    case SDL_SCANCODE_F1:
      if (SDL_KEYDOWN == event.type)
        SDL_SetRelativeMouseMode(SDL_TRUE);
      break;
    case SDL_SCANCODE_F2:
      if (SDL_KEYDOWN == event.type)
        SDL_SetRelativeMouseMode(SDL_FALSE);
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

void tesselated_ground(Engine& engine, float y_scale, float y_offset);

void DebugGui::update(Engine& engine, Game& game)
{
  ImGuiIO& io = ImGui::GetIO();

  {
    SDL_Window* window = engine.window;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));

    int    mx, my;
    Uint32 mouseMask               = SDL_GetMouseState(&mx, &my);
    bool   is_mouse_in_window_area = 0 < (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS);
    io.MousePos = is_mouse_in_window_area ? ImVec2((float)mx, (float)my) : ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = mousepressed[0] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)));
    io.MouseDown[1] = mousepressed[1] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)));
    io.MouseDown[2] = mousepressed[2] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)));

    for (bool& iter : mousepressed)
    {
      iter = false;
    }

    if (SDL_GetWindowFlags(window) & Uint32(SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MOUSE_CAPTURE))
    {
      io.MousePos = ImVec2((float)mx, (float)my);
    }

    const Uint32 window_has_mouse_captured = SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE;
    if (std::any_of(io.MouseDown, &io.MouseDown[5], [](bool it) { return it; }))
    {
      if (not window_has_mouse_captured)
      {
        SDL_CaptureMouse(SDL_TRUE);
      }
    }
    else
    {
      if (window_has_mouse_captured)
      {
        SDL_CaptureMouse(SDL_FALSE);
      }
    }

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor or (ImGuiMouseCursor_None == cursor))
    {
      SDL_ShowCursor(0);
    }
    else
    {
      SDL_SetCursor(mousecursors[cursor] ? mousecursors[cursor] : mousecursors[ImGuiMouseCursor_Arrow]);
      SDL_ShowCursor(1);
    }

    SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);
  }

  ImGui::NewFrame();

  ImGui::Begin("profiler");
  {
    static char highlight_filter[64];

    ImGui::SetCursorPos(ImVec2(5, 20.0f));
    ImGui::Text("Update");
    ImGui::Separator();
    profiler_visualize(game.update_profiler, "update", highlight_filter, 40.0f);
    ImGui::SetCursorPos(ImVec2(5, 100.0f));
    ImGui::Text("Render");
    ImGui::Separator();
    profiler_visualize(game.render_profiler, "render", highlight_filter, 120.0f);
    ImGui::SetCursorPos(ImVec2(5, 200.0f));

    if (ImGui::Button("pause"))
    {
      game.update_profiler.paused = !game.update_profiler.paused;
      game.render_profiler.paused = !game.render_profiler.paused;
    }

    ImGui::SetCursorPos(ImVec2(5, 230.0f));
    ImGui::InputText("filter", highlight_filter, SDL_arraysize(highlight_filter));
  }
  ImGui::End();

  ImGui::Begin("Main Panel");
  if (ImGui::CollapsingHeader("Animations"))
  {
    const char*   names[]    = {"CUBE", "RIGGED", "MONSTER"};
    SimpleEntity* entities[] = {&game.matrioshka_entity, &game.rigged_simple_entity, &game.monster_entity};

    for (uint32_t i = 0; i < 3; ++i)
    {
      SimpleEntity* e = entities[i];
      if (i > 0)
      {
        ImGui::SameLine();
      }
      if (ImGui::Button(names[i]))
      {
        if (0 == (e->flags & SimpleEntity::AnimationStartTime))
        {
          e->animation_start_time = game.current_time_sec;
          e->flags |= SimpleEntity::AnimationStartTime;
        }
      }
    }
  }

  if (ImGui::CollapsingHeader("Gameplay features"))
  {
    ImGui::Text("Booster jet fluel");
    ImGui::ProgressBar(game.booster_jet_fuel);
    ImGui::Text("%d %d | %d %d", game.lmb_last_cursor_position[0], game.lmb_last_cursor_position[1],
                game.lmb_current_cursor_position[0], game.lmb_current_cursor_position[1]);
  }

  if (ImGui::CollapsingHeader("Memory"))
  {
    auto bytes_as_mb = [](uint32_t in) { return in / (1024u * 1024u); };

    ImGui::Text("[GPU] image memory (%uMB pool)", bytes_as_mb(engine.memory_blocks.device_images.allocator.max_size));
    gpu_memory_visualize(engine.memory_blocks.device_images.allocator);

    ImGui::Text("[GPU] device-visible memory (%uMB pool)",
                bytes_as_mb(engine.memory_blocks.device_local.allocator.max_size));
    gpu_memory_visualize(engine.memory_blocks.device_local.allocator);

    ImGui::Text("[GPU] host-visible memory (%uMB pool)",
                bytes_as_mb(engine.memory_blocks.host_coherent.allocator.max_size));
    gpu_memory_visualize(engine.memory_blocks.host_coherent.allocator);

    ImGui::Text("[GPU] UBO memory (%uMB pool)", bytes_as_mb(engine.memory_blocks.host_coherent_ubo.allocator.max_size));
    gpu_memory_visualize(engine.memory_blocks.host_coherent_ubo.allocator);

    ImGui::Text("[HOST] general purpose allocator (%uMB pool)",
                bytes_as_mb(engine.generic_allocator.FREELIST_ALLOCATOR_CAPACITY_BYTES));
    free_list_visualize(engine.generic_allocator);
  }

  if (ImGui::RadioButton("debug flag 1", game.DEBUG_FLAG_1))
  {
    game.DEBUG_FLAG_1 = !game.DEBUG_FLAG_1;
  }

  if (ImGui::RadioButton("debug flag 2", game.DEBUG_FLAG_2))
  {
    game.DEBUG_FLAG_2 = !game.DEBUG_FLAG_2;
  }

  ImGui::InputFloat2("debug vec2", game.DEBUG_VEC2);
  ImGui::InputFloat2("debug vec2 additional", game.DEBUG_VEC2_ADDITIONAL);
  ImGui::InputFloat4("light ortho projection", game.DEBUG_LIGHT_ORTHO_PARAMS);

  // Resolution change
  {
    static int         current_resolution = 0;
    static const char* resolution_names[] = {"1200x900  (custom dev)", "1280x720  (HD)", "1366x768  (WXGA)",
                                             "1600x900  (HD+)", "1920x1080 (Full HD)"};
    if (ImGui::Combo("resolutions", &current_resolution, resolution_names, SDL_arraysize(resolution_names)))
    {
      SDL_Log("Resolution change: %s", resolution_names[current_resolution]);
      static const VkExtent2D resolutions[] = {
          {1200, 900}, {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080},
      };
      engine.change_resolution(resolutions[current_resolution]);
      game.player.camera_projection.perspective(engine.extent2D, to_rad(90.0f), 0.1f, 1000.0f);
    }
  }

  if (ImGui::CollapsingHeader("Ground testing"))
  {
    static float y_scale  = 0.0f;
    static float y_offset = 0.0f;
    ImGui::InputFloat("y_scale", &y_scale);
    ImGui::InputFloat("y_offset", &y_offset);

    if (ImGui::Button("Reload tesselation pipeline"))
    {
      vkDeviceWaitIdle(engine.device);
      vkDestroyPipeline(engine.device, engine.pipelines.tesselated_ground.pipeline, nullptr);
      tesselated_ground(engine, y_scale, y_offset);
    }
  }

  ImGui::Text("%.4f %.4f %.4f", game.player.position.x, game.player.position.y, game.player.position.z);

  // players position becomes the cartesian (0, 0) point for us, hence the substraction order
  // vec2 distance = {};
  // vec2_sub(distance, robot_position, player_position);

  // normalization helps to
  // vec2 normalized = {};
  // vec2_norm(normalized, distance);

  ImGui::End();
}

class DrawDataView
{
public:
  explicit DrawDataView(ImDrawData* draw_data)
      : draw_data(draw_data)
  {
  }

  [[nodiscard]] ImDrawList** begin() const { return draw_data->CmdLists; }
  [[nodiscard]] ImDrawList** end() const { return &draw_data->CmdLists[draw_data->CmdListsCount]; }

private:
  ImDrawData* draw_data;
};

template <typename T> T* serialize(T* dst, const ImVector<T>& src)
{
  SDL_memcpy(dst, src.Data, src.Size * sizeof(T));
  return dst + src.Size;
}

void DebugGui::render(Engine& engine, Game& game)
{
  ImDrawData* draw_data = ImGui::GetDrawData();

  const size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  const size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  SDL_assert(IMGUI_VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
  SDL_assert(IMGUI_INDEX_BUFFER_CAPACITY_BYTES >= index_size);

  DrawDataView view(draw_data);

  if (0 < vertex_size)
  {
    MemoryMap vtx_dst(engine.device, engine.memory_blocks.host_coherent.memory,
                      game.materials.imgui_vertex_buffer_offsets[game.image_index], vertex_size);

    std::accumulate(view.begin(), view.end(), reinterpret_cast<ImDrawVert*>(*vtx_dst),
                    [](ImDrawVert* dst, const ImDrawList* cmd_list) { return serialize(dst, cmd_list->VtxBuffer); });
  }

  if (0 < index_size)
  {
    MemoryMap idx_dst(engine.device, engine.memory_blocks.host_coherent.memory,
                      game.materials.imgui_index_buffer_offsets[game.image_index], index_size);

    std::accumulate(view.begin(), view.end(), reinterpret_cast<ImDrawIdx*>(*idx_dst),
                    [](ImDrawIdx* dst, const ImDrawList* cmd_list) { return serialize(dst, cmd_list->IdxBuffer); });
  }
}
