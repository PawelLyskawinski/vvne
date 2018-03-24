#pragma once

#include "engine.hh"
#include "gltf.hh"
#include "imgui.h"
#include <SDL2/SDL_mouse.h>

struct Game
{
  explicit Game(Engine& engine)
      : engine(engine)
  {
  }

  Engine&     engine;
  bool        mousepressed[3]                       = {};
  SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_] = {};
  uint64_t    time                                  = 0;

  int font_texture_idx         = 0;
  int clouds_texture_idx       = 0;
  int clouds_bliss_texture_idx = 0;

  gltf::Model           helmet                = {};
  gltf::RenderableModel renderableHelmet      = {};
  float                 helmet_translation[3] = {};

  void startup();
  void teardown();
  void update(float current_time_sec);
  void render(float current_time_sec);
};
