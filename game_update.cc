#include "engine.hh"
#include "game.hh"
#include <SDL2/SDL_events.h>

void game_update(Game& game, Engine& engine, float current_time_sec)
{
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
          game.mousepressed[0] = true;
          break;
        case SDL_BUTTON_RIGHT:
          game.mousepressed[1] = true;
          break;
        case SDL_BUTTON_MIDDLE:
          game.mousepressed[2] = true;
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
    int w, h;
    SDL_GetWindowSize(engine.window, &w, &h);

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    int    mx, my;
    Uint32 mouseMask               = SDL_GetMouseState(&mx, &my);
    bool   is_mouse_in_window_area = 0 < (SDL_GetWindowFlags(engine.window) & SDL_WINDOW_MOUSE_FOCUS);
    io.MousePos = is_mouse_in_window_area ? ImVec2((float)mx, (float)my) : ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = game.mousepressed[0] || (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    io.MouseDown[1] = game.mousepressed[1] || (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    io.MouseDown[2] = game.mousepressed[2] || (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;

    for (bool& iter : game.mousepressed)
      iter = false;

    if ((SDL_GetWindowFlags(engine.window) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MOUSE_CAPTURE)) != 0)
      io.MousePos = ImVec2((float)mx, (float)my);
    bool any_mouse_button_down = false;
    for (int n = 0; n < IM_ARRAYSIZE(io.MouseDown); n++)
      any_mouse_button_down |= io.MouseDown[n];
    if (any_mouse_button_down && (SDL_GetWindowFlags(engine.window) & SDL_WINDOW_MOUSE_CAPTURE) == 0)
      SDL_CaptureMouse(SDL_TRUE);
    if (!any_mouse_button_down && (SDL_GetWindowFlags(engine.window) & SDL_WINDOW_MOUSE_CAPTURE) != 0)
      SDL_CaptureMouse(SDL_FALSE);

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || ImGuiMouseCursor_None == cursor)
    {
      SDL_ShowCursor(0);
    }
    else
    {
      SDL_SetCursor(game.mousecursors[cursor] ? game.mousecursors[cursor] : game.mousecursors[ImGuiMouseCursor_Arrow]);
      SDL_ShowCursor(1);
    }

    SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);
  }

  ImGui::NewFrame();
  ImGui::Text("Hello World!");
  ImGui::Text("current time: %.2f", current_time_sec);
  ImGui::SliderFloat3("helmet position", game.helmet_translation, -20.0f, 20.0f);
}
