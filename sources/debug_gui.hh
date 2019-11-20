#pragma once

#include <SDL2/SDL_events.h>
#include <imgui.h>

struct Engine;
struct Game;

struct DebugGui
{
  bool        mousepressed[3];
  SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];
  bool        engine_console_open;
  bool        story_editor_tab_selected;

  void setup();
  void teardown();
  void process_event(Game& game, SDL_Event& event);
  void update(Engine& engine, Game& game);

  static void render(Engine& engine, Game& game);
};
