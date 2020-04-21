#pragma once

#include "debug_gui.hh"
#include "levels/example_level.hh"
#include "materials.hh"
#include "player.hh"
#include "profiler.hh"
#include "simple_entity.hh"
#include "story_editor.hh"
#include "vtl/atomic_stack.hh"
#include "vtl/priority_pair.hh"

struct ShadowmapCommandBuffer
{
  VkCommandBuffer cmd;
  int             cascade_idx;
};

using PrioritizedCommandBuffer     = PriorityPair<VkCommandBuffer>;
using PrioritizedCommandBufferList = AtomicStack<PrioritizedCommandBuffer, 64>;

struct Game;

struct JobContext
{
  Engine* engine;
  Game*   game;
};

struct Game
{
  DebugGui           debug_gui;
  Materials          materials;
  Profiler           update_profiler;
  Profiler           render_profiler;
  story::StoryEditor story;

  VkCommandBuffer                         primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
  uint32_t                                image_index;
  AtomicStack<ShadowmapCommandBuffer, 64> shadow_mapping_pass_commands;
  VkCommandBuffer                         skybox_command;
  PrioritizedCommandBufferList            scene_rendering_commands;
  PrioritizedCommandBufferList            gui_commands;
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
  // Gameplay
  //////////////////////////////////////////////////////////////////////////////

  Player       player;
  ExampleLevel level;

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float time_delta_since_last_frame_ms);
  void render(Engine& engine);
  void record_primary_command_buffer(Engine& engine);
};
