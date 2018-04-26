#pragma once

#include "engine.hh"
#include "gltf.hh"
#include "imgui.h"
#include <SDL2/SDL_mouse.h>

struct Game
{
  struct DebugGui
  {
    bool        mousepressed[3];
    SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];
    int         font_texture_idx;

    enum
    {
      VERTEX_BUFFER_CAPACITY_BYTES = 40 * 1024,
      INDEX_BUFFER_CAPACITY_BYTES  = 30 * 1024
    };

    VkDeviceSize vertex_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
    VkDeviceSize index_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
  } debug_gui;

  VkDescriptorSet skybox_dset;
  VkDescriptorSet helmet_dset;
  VkDescriptorSet imgui_dset;

  gltf::RenderableModel helmet;
  gltf::RenderableModel box;
  gltf::RenderableModel robot;

  float robot_position[3];
  int environment_hdr_map_idx; // radiance, quick and fast
  // int environment_equirectangular_texture_idx; // highly detailed, takes ~100MB on gpu ram
  int environment_cubemap_idx;
  int irradiance_cubemap_idx;
  int prefiltered_cubemap_idx;
  int brdf_lookup_idx;

  float update_times[50];
  float render_times[50];
  float helmet_translation[3];

  struct LightSource
  {
    float position[3];
    float color[3];

    void setPosition(float x, float y, float z)
    {
      position[0] = x;
      position[1] = y;
      position[2] = z;
    }

    void setColor(float r, float g, float b)
    {
      color[0] = r;
      color[1] = g;
      color[2] = b;
    }
  };

  LightSource  light_sources[10];
  int          light_sources_count;
  VkDeviceSize lights_ubo_offset;

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float current_time_sec);
  void render(Engine& engine, float current_time_sec);
};
