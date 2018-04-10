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

  gltf::Model helmet;
  gltf::Model box;

  gltf::RenderableModel renderableHelmet;
  gltf::RenderableModel renderableBox;

  int environment_hdr_map_idx;
  int environment_equirectangular_texture_idx;
  // (For some reason 4-mb jpeg takes ~100MB on gpu. WTF!? todo: investigate)

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
