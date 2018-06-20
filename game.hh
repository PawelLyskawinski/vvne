#pragma once

#include "engine.hh"
#include "gltf.hh"
#include "imgui.h"
#include <SDL2/SDL_mouse.h>

struct LightSources
{
  vec4 positions[64];
  vec4 colors[64];
  int  count;
};

struct Game
{
  struct DebugGui
  {
    bool        mousepressed[3];
    SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];
    int         font_texture_idx;

    enum
    {
      VERTEX_BUFFER_CAPACITY_BYTES = 100 * 1024,
      INDEX_BUFFER_CAPACITY_BYTES  = 80 * 1024
    };

    VkDeviceSize vertex_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
    VkDeviceSize index_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
  } debug_gui;

  // materials
  VkDescriptorSet pbr_ibl_environment_dset;
  VkDescriptorSet helmet_pbr_material_dset;
  VkDescriptorSet robot_pbr_material_dset;
  VkDescriptorSet pbr_dynamic_lights_dset;
  VkDescriptorSet skybox_cubemap_dset;
  VkDescriptorSet imgui_font_atlas_dset;
  VkDescriptorSet rig_skinning_matrices_dset;
  VkDescriptorSet monster_skinning_matrices_dset;

  // ubos
  VkDeviceSize rig_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize fig_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize monster_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize pbr_dynamic_lights_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];

  // frame cache
  LightSources pbr_light_sources_cache;

  // models
  gltf::RenderableModel helmet;
  gltf::RenderableModel box;
  gltf::RenderableModel animatedBox;
  gltf::RenderableModel riggedSimple;
  gltf::RenderableModel monster;
  gltf::RenderableModel robot;

  // textures
  int environment_cubemap_idx;
  int irradiance_cubemap_idx;
  int prefiltered_cubemap_idx;
  int brdf_lookup_idx;

  float robot_position[3];
  float rigged_position[3];
  float helmet_translation[3];

  float update_times[50];
  float render_times[50];

  mat4x4 projection;
  mat4x4 view;
  vec3   camera_position;

  VkDeviceSize vr_level_vertex_buffer_offset;
  VkDeviceSize vr_level_index_buffer_offset;
  int          vr_level_index_count;
  VkIndexType  vr_level_index_type;
  vec2         vr_level_entry;
  vec2         vr_level_goal;

  vec3  player_position;
  quat  player_orientation;
  vec3  player_velocity;
  vec3  player_acceleration;
  float camera_angle;
  float camera_updown_angle;

  bool player_forward_pressed;
  bool player_back_pressed;
  bool player_strafe_left_pressed;
  bool player_strafe_right_pressed;
  bool player_booster_activated;

  // gameplay mechanics
  float booster_jet_fuel;

  bool lmb_clicked;
  int  lmb_last_cursor_position[2];
  int  lmb_current_cursor_position[2];

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float current_time_sec, float time_delta_since_last_frame);
  void render(Engine& engine, float current_time_sec);
};
