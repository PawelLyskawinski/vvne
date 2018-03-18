#pragma once

#include "engine.hh"
#include "gltf.hh"
#include "imgui.h"
#include <SDL2/SDL_mouse.h>

struct Game
{
  bool        mousepressed[3];
  SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];
  uint64_t    time;
  int         font_texture_idx;
  int         clouds_texture_idx;
  int         clouds_bliss_texture_idx;

  gltf::Model           helmet;
  gltf::RenderableModel renderableHelmet;
  float helmet_translation[3];
};

void game_startup(Game& game, Engine& engine);
void game_teardown(Game& game, Engine& engine);
void game_update(Game& game, Engine& engine, float current_time_sec);
void game_render(Game& game, Engine& engine, float current_time_sec);