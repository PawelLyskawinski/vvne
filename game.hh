#pragma once

#include "ecs.hh"
#include "engine.hh"
#include "gltf.hh"
#include "imgui.h"
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_thread.h>

#define WORKER_THREADS_COUNT 3

class LinearAllocator
{
public:
  explicit LinearAllocator(size_t size)
      : memory(reinterpret_cast<uint8_t*>(SDL_malloc(size)))
      , bytes_used(0)
  {
  }

  ~LinearAllocator()
  {
    SDL_free(memory);
  }

  template <typename T> T* allocate(int count = 1)
  {
    T*  result         = reinterpret_cast<T*>(&memory[bytes_used]);
    int size           = count * sizeof(T);
    int padding        = (size % 8) ? 8 - (size % 8) : 0;
    int corrected_size = size + padding;
    bytes_used += corrected_size;
    return result;
  }

  void reset()
  {
    bytes_used = 0;
  }

private:
  uint8_t* memory;
  int      bytes_used;
};

struct LightSources
{
  vec4 positions[64];
  vec4 colors[64];
  int  count;
};

struct SdfChar
{
  uint8_t  width;
  uint8_t  height;
  uint16_t x;
  uint16_t y;
  int8_t   xoffset;
  int8_t   yoffset;
  uint8_t  xadvance;
};

struct GuiLine
{
  enum class Size
  {
    Big,
    Normal,
    Small,
    Tiny
  };

  enum class Color
  {
    Green,
    Red,
    Yellow
  };

  vec2  a;
  vec2  b;
  Size  size;
  Color color;
};

struct GuiLineSizeCount
{
  int big;
  int normal;
  int small;
  int tiny;
};

struct KeyMapping
{
  ImGuiKey_    imgui;
  SDL_Scancode sdl;
};

struct CursorMapping
{
  ImGuiMouseCursor_ imgui;
  SDL_SystemCursor  sdl;
};

struct GuiHeightRulerText
{
  vec2 offset;
  int  size;
  int  value;
};

struct GenerateSdfFontCommand
{
  char     character;
  uint8_t* lookup_table;
  SdfChar* character_data;
  int      characters_pool_count;
  int      texture_size[2];
  float    scaling;
  vec3     position;
  float    cursor;
};

struct GenerateSdfFontCommandResult
{
  vec2   character_coordinate;
  vec2   character_size;
  mat4x4 transform;
  float  cursor_movement;
};

struct GenerateGuiLinesCommand
{
  float      player_y_location_meters;
  float      camera_x_pitch_radians;
  float      camera_y_pitch_radians;
  VkExtent2D screen_extent2D;
};

struct Game;

struct ThreadJobData
{
  int              thread_id;
  Engine&          engine;
  Game&            game;
  LinearAllocator& allocator;
};

struct Job
{
  using Fcn = void (*)(ThreadJobData tjd);
  const char* name;
  Fcn         fcn;
};

struct ThreadJobStatistic
{
  int         threadId;
  float       duration_sec;
  const char* name;
};

struct JobSystem
{
  // general controls
  bool thread_end_requested;

  // Workload dispatching
  Job          jobs[64];
  int          jobs_max;
  SDL_atomic_t jobs_taken;

  // Synchronization
  SDL_cond*    new_jobs_available_cond;
  SDL_mutex*   new_jobs_available_mutex;
  SDL_sem*     all_threads_idle_signal;
  SDL_atomic_t threads_finished_work;

  // Worker thread resources
  SDL_Thread*     worker_threads[WORKER_THREADS_COUNT];
  VkCommandPool   worker_pools[WORKER_THREADS_COUNT];
  VkCommandBuffer commands[Engine::SWAPCHAIN_IMAGES_COUNT][WORKER_THREADS_COUNT][64];
  int             submited_command_count[Engine::SWAPCHAIN_IMAGES_COUNT][WORKER_THREADS_COUNT];

  // profiling data
  ThreadJobStatistic profile_data[64];
  SDL_atomic_t       profile_data_count;

  bool               is_profiling_paused;
  ThreadJobStatistic paused_profile_data[64];
  int                paused_profile_data_count;
};

struct RecordedCommandBuffer
{
  VkCommandBuffer command;
  VkRenderPass    render_pass;
  int             subpass;

  bool operator==(const RecordedCommandBuffer& rhs) const
  {
    return (render_pass == rhs.render_pass) and (subpass == rhs.subpass);
  }
};

struct SecondaryCommandBufferSink
{
  RecordedCommandBuffer commands[512];
  SDL_atomic_t          count;
};

struct RenderEntityParams
{
  VkCommandBuffer cmd;
  mat4x4          projection;
  mat4x4          view;
  vec3            camera_position;
  vec3            color;
  int             pipeline;
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

struct Game
{
  uint8_t lucida_sans_sdf_char_ids[97];
  SdfChar lucida_sans_sdf_chars[97];
  Texture lucida_sans_sdf_image;

  struct DebugGui
  {
    bool        mousepressed[3];
    SDL_Cursor* mousecursors[ImGuiMouseCursor_Count_];
    Texture     font_texture;

    enum
    {
      VERTEX_BUFFER_CAPACITY_BYTES = 100 * 1024,
      INDEX_BUFFER_CAPACITY_BYTES  = 80 * 1024
    };

    VkDeviceSize vertex_buffer_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
    VkDeviceSize index_buffer_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  } debug_gui;

  bool DEBUG_FLAG_1;
  bool DEBUG_FLAG_2;
  vec2 DEBUG_VEC2;
  vec2 DEBUG_VEC2_ADDITIONAL;
  vec4 DEBUG_LIGHT_ORTHO_PARAMS;

  // materials
  VkDescriptorSet pbr_ibl_environment_dset;
  VkDescriptorSet helmet_pbr_material_dset;
  VkDescriptorSet robot_pbr_material_dset;
  VkDescriptorSet pbr_dynamic_lights_dset;
  VkDescriptorSet skybox_cubemap_dset;
  VkDescriptorSet imgui_font_atlas_dset;
  VkDescriptorSet rig_skinning_matrices_dset;
  VkDescriptorSet monster_skinning_matrices_dset;
  VkDescriptorSet lucida_sans_sdf_dset;
  VkDescriptorSet sandy_level_pbr_material_dset;
  VkDescriptorSet pbr_water_material_dset;
  VkDescriptorSet debug_shadow_map_dset[Engine::SWAPCHAIN_IMAGES_COUNT];
  VkDescriptorSet light_space_matrices_dset[Engine::SWAPCHAIN_IMAGES_COUNT];

  // ubos
  VkDeviceSize rig_skinning_matrices_ubo_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize fig_skinning_matrices_ubo_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize monster_skinning_matrices_ubo_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize pbr_dynamic_lights_ubo_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize light_space_matrices_ubo_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];

  // frame cache
  LightSources pbr_light_sources_cache;

  // models
  SceneGraph helmet;
  SceneGraph box;
  SceneGraph animatedBox;
  SceneGraph riggedSimple;
  SceneGraph monster;
  SceneGraph robot;

  // textures
  Texture environment_cubemap;
  Texture irradiance_cubemap;
  Texture prefiltered_cubemap;
  Texture brdf_lookup;

  Texture sand_albedo;
  Texture sand_ambient_occlusion;
  Texture sand_metallic_roughness;
  Texture sand_normal;
  Texture sand_emissive;

  Texture water_normal;

  // textures - game
  VkDeviceSize green_gui_billboard_vertex_buffer_offset;
  VkDeviceSize regular_billboard_vertex_buffer_offset;

  vec2  green_gui_radar_position;
  float green_gui_radar_rotation;

  float robot_position[3];
  float rigged_position[3];
  float light_source_position[3];

  float update_times[50];
  float render_times[50];

  mat4x4 projection;
  mat4x4 view;
  vec3   camera_position;
  mat4x4 light_space_matrix;

  VkDeviceSize vr_level_vertex_buffer_offset;
  VkDeviceSize vr_level_index_buffer_offset;
  int          vr_level_index_count;
  VkIndexType  vr_level_index_type;
  vec2         vr_level_entry;
  vec2         vr_level_goal;

  VkDeviceSize     green_gui_rulers_buffer_offsets[Engine::SWAPCHAIN_IMAGES_COUNT];
  GuiLineSizeCount gui_green_lines_count;
  GuiLineSizeCount gui_red_lines_count;
  GuiLineSizeCount gui_yellow_lines_count;

  vec3  player_position;
  vec3  player_velocity;
  vec3  player_acceleration;
  float camera_angle;
  float camera_updown_angle;
  bool  player_jumping;
  float player_jump_start_timestamp_sec;
  float radar_scale;

  bool player_forward_pressed;
  bool player_back_pressed;
  bool player_jump_pressed;
  bool player_strafe_left_pressed;
  bool player_strafe_right_pressed;
  bool player_booster_activated;

  // gameplay mechanics
  float booster_jet_fuel;

  bool lmb_clicked;
  int  lmb_last_cursor_position[2];
  int  lmb_current_cursor_position[2];

  uint32_t                   image_index;
  JobSystem                  js;
  SecondaryCommandBufferSink js_sink;
  float                      current_time_sec;
  float                      diagnostic_meas_scale;

  EntityComponentSystem ecs;

  Entity helmet_entity;
  Entity robot_entity;
  Entity monster_entity;
  Entity box_entities[6];
  Entity matrioshka_entity;
  Entity rigged_simple_entity;

  WeaponSelection weapon_selections[2];

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float time_delta_since_last_frame_ms);
  void render(Engine& engine);
};
