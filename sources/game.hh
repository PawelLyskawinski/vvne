#pragma once

#include "SimpleEntity.hh"
#include "debug_gui.hh"
#include "engine/atomic_stack.hh"
#include "engine/engine.hh"
#include "engine/gltf.hh"
#include "game_constants.hh"
#include "game_generate_gui_lines.hh"
#include "imgui.h"
#include "levels/example_level.hh"
#include "materials.hh"
#include "player.hh"
#include "profiler.hh"
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_thread.h>

struct ShadowmapCommandBuffer
{
  VkCommandBuffer cmd;
  int             cascade_idx;
};

struct RenderEntityParams
{
  RenderEntityParams() = default;
  explicit RenderEntityParams(const Player& player);

  VkCommandBuffer  cmd = VK_NULL_HANDLE;
  Mat4x4           projection;
  Mat4x4           view;
  Vec3             camera_position;
  Vec3             color;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
};

struct WeaponSelection
{
public:
  void init();
  void select(int new_dst);
  void animate(float step);
  void calculate(float transparencies[3]);

private:
  int   src;
  int   dst;
  bool  switch_animation;
  float switch_animation_time;
};

struct Game;

struct JobContext
{
  Engine* engine;
  Game*   game;
};

struct Game
{
  DebugGui  debug_gui;
  Materials materials;
  Profiler  update_profiler;
  Profiler  render_profiler;

  VkCommandBuffer                         primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
  uint32_t                                image_index;
  AtomicStack<ShadowmapCommandBuffer, 64> shadow_mapping_pass_commands;
  VkCommandBuffer                         skybox_command;
  AtomicStack<VkCommandBuffer, 64>        scene_rendering_commands;
  AtomicStack<VkCommandBuffer, 64>        gui_commands;
  float                                   current_time_sec;
  JobContext                              job_context;

  bool DEBUG_FLAG_1;
  bool DEBUG_FLAG_2;
  Vec2 DEBUG_VEC2;
  Vec2 DEBUG_VEC2_ADDITIONAL;
  Vec4 DEBUG_LIGHT_ORTHO_PARAMS;

  bool lmb_clicked;
  int  lmb_last_cursor_position[2];
  int  lmb_current_cursor_position[2];

  //////////////////////////////////////////////////////////////////////////////
  // Gameplay logic
  //////////////////////////////////////////////////////////////////////////////

  Player       player;
  ExampleLevel level;

  // gameplay mechanics
  float           booster_jet_fuel;
  WeaponSelection weapon_selections[2];

  SimpleEntity helmet_entity;
  SimpleEntity robot_entity;

  // light test
  SimpleEntity box_entities[7];

  SimpleEntity matrioshka_entity;
  SimpleEntity monster_entity;
  SimpleEntity rigged_simple_entity;
  SimpleEntity axis_arrow_entities[3];

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float time_delta_since_last_frame_ms);
  void render(Engine& engine);
};
