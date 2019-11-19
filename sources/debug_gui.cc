#include "debug_gui.hh"
#include "engine/engine.hh"
#include "engine/free_list_visualizer.hh"
#include "engine/gpu_memory_visualizer.hh"
#include "engine/memory_map.hh"
#include "game.hh"
#include "profiler_visualizer.hh"
#include <SDL2/SDL_clipboard.h>

#include <SDL2/SDL_log.h>
#include <algorithm>
#include <numeric>

void DebugGui::setup()
{
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::StyleColorsClassic();

  {
    struct KeyMapping
    {
      ImGuiKey_    imgui;
      SDL_Scancode sdl;
    };

    KeyMapping mappings[] = {
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
        {ImGuiKey_Z, SDL_SCANCODE_Z},
    };

    for (KeyMapping mapping : mappings)
      io.KeyMap[mapping.imgui] = mapping.sdl;
  }

  io.RenderDrawListsFn  = nullptr;
  io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
  io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
  io.ClipboardUserData  = nullptr;

  {
    struct CursorMapping
    {
      ImGuiMouseCursor_ imgui;
      SDL_SystemCursor  sdl;
    };

    CursorMapping mappings[] = {
        {ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW},
        {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM},
        {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL},
        {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS},
        {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE},
        {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW},
        {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE},
    };

    for (CursorMapping mapping : mappings)
    {
      mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
    }
  }
}

void DebugGui::teardown()
{
  for (SDL_Cursor* cursor : mousecursors)
  {
    SDL_FreeCursor(cursor);
  }
}

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

  case SDL_MOUSEBUTTONDOWN: {
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
  case SDL_KEYUP: {
    io.KeysDown[event.key.keysym.scancode] = (SDL_KEYDOWN == event.type);

    io.KeyShift = SDL_GetModState() & SDL_Keymod(KMOD_SHIFT);
    io.KeyCtrl  = SDL_GetModState() & SDL_Keymod(KMOD_CTRL);
    io.KeyAlt   = SDL_GetModState() & SDL_Keymod(KMOD_ALT);
    io.KeySuper = SDL_GetModState() & SDL_Keymod(KMOD_GUI);

    switch (event.key.keysym.scancode)
    {
    case SDL_SCANCODE_GRAVE:
      if ((SDL_KEYUP == event.type))
      {
        engine_console_open = !engine_console_open;
        if (engine_console_open)
        {
          SDL_SetRelativeMouseMode(SDL_FALSE);
        }
        else
        {
          SDL_SetRelativeMouseMode(SDL_TRUE);
        }
      }
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

namespace {

uint32_t bytes_as_mb(uint32_t in) { return in / (1024u * 1024u); }

void draw_performence_tab(Engine& engine, Game& game)
{
  static char highlight_filter[64];

  ImGui::Text("Update");
  ImGui::Separator();
  profiler_visualize(game.update_profiler, "update", highlight_filter, 50.0f);

  // Cursor reposition is needed because the rendering above finishes up with variable height.
  // In the end everything rendered afterwards shakes teremdously without this
  ImGui::SetCursorPos(ImVec2(5, 100.0f));
  ImGui::Text("Render");
  ImGui::Separator();
  profiler_visualize(game.render_profiler, "render", highlight_filter, 130.0f);

  ImGui::SetCursorPos(ImVec2(5, 200.0f));
  if (ImGui::Button("pause"))
  {
    game.update_profiler.paused = !game.update_profiler.paused;
    game.render_profiler.paused = !game.render_profiler.paused;
  }

  ImGui::SetCursorPos(ImVec2(5, 230.0f));
  ImGui::InputText("filter", highlight_filter, SDL_arraysize(highlight_filter));
  ImGui::Separator();

  auto gpu_mem_printer = [](const char* name, GpuMemoryAllocator& allocator) {
    ImGui::Text("[GPU] %s memory (%uMB pool)", name, bytes_as_mb(allocator.max_size));
    gpu_memory_visualize(allocator);
  };

  gpu_mem_printer("image", engine.memory_blocks.device_images.allocator);
  gpu_mem_printer("device-visible", engine.memory_blocks.device_local.allocator);
  gpu_mem_printer("host-visible", engine.memory_blocks.host_coherent.allocator);
  gpu_mem_printer("UBO", engine.memory_blocks.host_coherent_ubo.allocator);

  ImGui::Text("[HOST] general purpose allocator (%uMB pool)",
              bytes_as_mb(engine.generic_allocator.FREELIST_ALLOCATOR_CAPACITY_BYTES));
  free_list_visualize(engine.generic_allocator);
}

void draw_debug_tab(Engine& engine, Game& game)
{
  ImGui::Text("Animations");
  ImGui::Separator();
  {
    const char*   names[]    = {"CUBE", "RIGGED", "MONSTER"};
    SimpleEntity* entities[] = {&game.level.matrioshka_entity, &game.level.rigged_simple_entity,
                                &game.level.monster_entity};

    for (uint32_t i = 0; i < 3; ++i)
    {
      SimpleEntity* e = entities[i];
      if (i > 0)
      {
        ImGui::SameLine();
      }
      if (ImGui::Button(names[i]))
      {
        if (not e->flags.animation_start_time)
        {
          e->animation_start_time       = game.current_time_sec;
          e->flags.animation_start_time = true;
        }
      }
    }
  }

  ImGui::Text("Debug Variables");
  ImGui::Separator();
  if (ImGui::RadioButton("debug flag 1", game.DEBUG_FLAG_1))
  {
    game.DEBUG_FLAG_1 = !game.DEBUG_FLAG_1;
  }

  if (ImGui::RadioButton("debug flag 2", game.DEBUG_FLAG_2))
  {
    game.DEBUG_FLAG_2 = !game.DEBUG_FLAG_2;
  }

  ImGui::InputFloat2("debug vec2", &game.DEBUG_VEC2.x);
  ImGui::InputFloat2("debug vec2 additional", &game.DEBUG_VEC2_ADDITIONAL.x);
  ImGui::InputFloat4("light ortho projection", &game.DEBUG_LIGHT_ORTHO_PARAMS.x);

  ImGui::Text("Resolution");
  ImGui::Separator();
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

  ImGui::Text("Info");
  ImGui::Separator();
  ImGui::Text("%.4f %.4f %.4f", game.player.position.x, game.player.position.y, game.player.position.z);
  ImGui::Text("acceleration len: %.4f", game.player.acceleration.len());

  ImGui::Text("Profiler");
  ImGui::Separator();
  ImGui::InputInt("update lag", &game.update_profiler.skip_frames);
  ImGui::InputInt("render lag", &game.render_profiler.skip_frames);
}

} // namespace

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

  if (!engine_console_open)
  {
    return;
  }

  ImGui::SetNextWindowPos({0, 0});
  const ImVec2 extent_as_vec2(engine.extent2D.width, engine.extent2D.height);
  ImGui::SetNextWindowSize(extent_as_vec2);

  ImGui::Begin("engine console", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
  {
    if (ImGui::BeginTabBar("main selector"))
    {
      if (ImGui::BeginTabItem("Story Editor"))
      {
        game.story_data.editor_render();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Profiler"))
      {
        draw_performence_tab(engine, game);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Debug"))
      {
        draw_debug_tab(engine, game);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
  }
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
