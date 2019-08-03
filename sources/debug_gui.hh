#pragma once

#include <SDL2/SDL_events.h>
#include <imgui.h>

struct Engine;
struct Game;

struct DebugGui
{
  bool        mousepressed[3];
  SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];

  void process_event(SDL_Event& event);
  void update(Engine& engine, Game& game);

  static void render(Engine& engine, Game& game);
};
