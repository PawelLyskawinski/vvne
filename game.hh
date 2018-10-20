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

template <typename T, int SIZE> struct AtomicStack
{
  void push(const T& in)
  {
    stack[SDL_AtomicIncRef(&count)] = in;
  }

  T* begin()
  {
    return stack;
  }

  T* end()
  {
    return &stack[SDL_AtomicGet(&count)];
  }

  void reset()
  {
    SDL_AtomicSet(&count, 0);
  }

  T            stack[SIZE];
  SDL_atomic_t count;
};

struct ThreadJobData
{
  int              thread_id;
  Engine&          engine;
  Game&            game;
  LinearAllocator& allocator;
};

struct JobSystem
{
  using JobFunction = void (*)(ThreadJobData tjd);

  // general controls
  bool thread_end_requested;

  // Workload dispatching
  const char*  job_names[128];
  JobFunction  jobs[128];
  int          jobs_max;
  SDL_atomic_t jobs_taken;

  void push(const char* name, JobFunction job)
  {
    job_names[jobs_max] = name;
    jobs[jobs_max]      = job;
    jobs_max += 1;
  }

  // Synchronization
  SDL_cond*    new_jobs_available_cond;
  SDL_mutex*   new_jobs_available_mutex;
  SDL_sem*     all_threads_idle_signal;
  SDL_atomic_t threads_finished_work;

  // Worker thread resources
  SDL_Thread*     worker_threads[WORKER_THREADS_COUNT];
  VkCommandPool   worker_pools[WORKER_THREADS_COUNT];
  VkCommandBuffer commands[SWAPCHAIN_IMAGES_COUNT][WORKER_THREADS_COUNT][64];
  int             submited_command_count[SWAPCHAIN_IMAGES_COUNT][WORKER_THREADS_COUNT];

  struct ProfileData
  {
    int         thread_ids[128];
    float       duration_sec[128];
    const char* job_names[128];

    void copy_from(const ProfileData& other, const int count)
    {
      for (int i = 0; i < count; ++i)
        thread_ids[i] = other.thread_ids[i];

      for (int i = 0; i < count; ++i)
        duration_sec[i] = other.duration_sec[i];

      for (int i = 0; i < count; ++i)
        job_names[i] = other.job_names[i];
    }
  };

  // profiling data
  ProfileData  profile_each_frame;
  SDL_atomic_t profile_each_frame_counter;

  // paused profiling data
  bool        is_profiling_paused;
  ProfileData paused_profile_data;
  int         paused_profile_data_count;

  // common read-only access to the samples
  ProfileData* selected_profile_data;
  int          selected_profile_data_count;

  void push_profile_data(int thread_id, float duration, const char* job_name)
  {
    const int idx = SDL_AtomicIncRef(&profile_each_frame_counter);

    profile_each_frame.thread_ids[idx]   = thread_id;
    profile_each_frame.duration_sec[idx] = duration;
    profile_each_frame.job_names[idx]    = job_name;
  }
};

struct ShadowmapCommandBuffer
{
  VkCommandBuffer cmd;
  int             cascade_idx;
};

struct RenderEntityParams
{
  VkCommandBuffer  cmd;
  mat4x4           projection;
  mat4x4           view;
  vec3             camera_position;
  vec3             color;
  VkPipelineLayout pipeline_layout;
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
  //////////////////////////////////////////////////////////////////////////////
  // Rendering resources
  //////////////////////////////////////////////////////////////////////////////

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
  VkDescriptorSet lucida_sans_sdf_dset;
  VkDescriptorSet sandy_level_pbr_material_dset;
  VkDescriptorSet pbr_water_material_dset;
  VkDescriptorSet debug_shadow_map_dset;

  // Those two descriptor sets partially point to the same data. In both cases we'll be using
  // already calculated and uploaded cascade view projection matrices. The difference is:
  // - during rendering additionally information about the depth split distance per cascade is required
  // - depth pass uses them in vertex shader, rendering in fragment. The stages itself require us to have separate
  //   descriptors
  //
  VkDescriptorSet cascade_view_proj_matrices_depth_pass_dset[SWAPCHAIN_IMAGES_COUNT];
  VkDescriptorSet cascade_view_proj_matrices_render_dset[SWAPCHAIN_IMAGES_COUNT];

  // ubos
  VkDeviceSize rig_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize fig_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize monster_skinning_matrices_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize pbr_dynamic_lights_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize cascade_view_proj_mat_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];

  // cascade shadow mapping
  mat4x4 cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT];
  float  cascade_split_depths[SHADOWMAP_CASCADE_COUNT];

  // CSM debuging mostly, but can be used as a billboard space in any shader
  VkDeviceSize green_gui_billboard_vertex_buffer_offset;
  VkDeviceSize regular_billboard_vertex_buffer_offset;

  VkCommandBuffer primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];

  // frame cache
  LightSources pbr_light_sources_cache;

  // models
  SceneGraph helmet;
  SceneGraph box;
  SceneGraph animatedBox;
  SceneGraph riggedSimple;
  SceneGraph monster;
  SceneGraph robot;
  SceneGraph rock;
  SceneGraph lil_arrow;

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

  VkDeviceSize vr_level_vertex_buffer_offset;
  VkDeviceSize vr_level_index_buffer_offset;
  int          vr_level_index_count;
  VkIndexType  vr_level_index_type;

  VkDeviceSize     green_gui_rulers_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
  GuiLineSizeCount gui_green_lines_count;
  GuiLineSizeCount gui_red_lines_count;
  GuiLineSizeCount gui_yellow_lines_count;

  uint32_t                                image_index;
  JobSystem                               js;
  AtomicStack<ShadowmapCommandBuffer, 64> shadow_mapping_pass_commands;
  VkCommandBuffer                         skybox_command;
  AtomicStack<VkCommandBuffer, 64>        scene_rendering_commands;
  AtomicStack<VkCommandBuffer, 64>        gui_commands;
  float                                   current_time_sec;
  float                                   diagnostic_meas_scale;

  Ecs ecs;

  //////////////////////////////////////////////////////////////////////////////
  // Gameplay logic
  //////////////////////////////////////////////////////////////////////////////

  vec2 vr_level_entry;
  vec2 vr_level_goal;

  bool DEBUG_FLAG_1;
  bool DEBUG_FLAG_2;
  vec2 DEBUG_VEC2;
  vec2 DEBUG_VEC2_ADDITIONAL;
  vec4 DEBUG_LIGHT_ORTHO_PARAMS;

  vec2  green_gui_radar_position;
  float green_gui_radar_rotation;

  float robot_position[3];
  float rigged_position[3];
  float light_source_position[3];

  float update_times[50];
  float render_times[50];

  enum class CameraState
  {
    Gameplay,
    LevelEditor,
  };

  CameraState camera_state;

  mat4x4& get_selected_camera_projection()
  {
    switch (camera_state)
    {
    default:
    case CameraState::Gameplay:
      return projection;
    case CameraState::LevelEditor:
      return editor_projection;
    }
  }

  mat4x4& get_selected_camera_view()
  {
    switch (camera_state)
    {
    default:
    case CameraState::Gameplay:
      return view;
    case CameraState::LevelEditor:
      return editor_view;
    }
  }

  vec3& get_selected_camera_position()
  {
    switch (camera_state)
    {
    default:
    case CameraState::Gameplay:
      return camera_position;
    case CameraState::LevelEditor:
      return editor_camera_position;
    }
  }

  // gameplay "behind the robot" view
  mat4x4 projection;
  mat4x4 view;
  vec3   camera_position;

  // editor view and camera
  mat4x4 editor_projection;
  mat4x4 editor_view;
  vec3   editor_camera_position;

  vec3  player_position;
  vec3  player_velocity;
  vec3  player_acceleration;
  float camera_angle;
  float camera_updown_angle;
  bool  player_jumping;
  float player_jump_start_timestamp_sec;
  float radar_scale;

  struct GameplayKeyFlags
  {
    enum : uint64_t
    {
      forward_pressed      = (1 << 0),
      back_pressed         = (1 << 1),
      jump_pressed         = (1 << 2),
      strafe_left_pressed  = (1 << 3),
      strafe_right_pressed = (1 << 4),
      booster_activated    = (1 << 5),
    };
  };

  uint64_t player_key_flags;

  struct EditorKeyFlags
  {
    enum : uint64_t
    {
      up_pressed    = (1 << 0),
      down_pressed  = (1 << 1),
      left_pressed  = (1 << 2),
      right_pressed = (1 << 3),
    };
  };

  uint64_t editor_key_flags;

  // gameplay mechanics
  float booster_jet_fuel;

  bool lmb_clicked;
  int  lmb_last_cursor_position[2];
  int  lmb_current_cursor_position[2];

  SimpleEntity  helmet_entity;
  SimpleEntity  robot_entity;
  SimpleEntity  box_entities[6];
  SimpleEntity  matrioshka_entity;
  SkinnedEntity monster_entity;
  SkinnedEntity rigged_simple_entity;
  SimpleEntity  axis_arrow_entities[3];

  WeaponSelection weapon_selections[2];

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float time_delta_since_last_frame_ms);
  void render(Engine& engine);
};
