#pragma once

#include "SimpleEntity.hh"
#include "engine/engine.hh"
#include "engine/gltf.hh"
#include "imgui.h"
#include "game_generate_gui_lines.hh"
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_thread.h>

constexpr uint32_t WORKER_THREADS_COUNT = 3;
constexpr uint32_t MAX_ROBOT_GUI_LINES = 400;

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

struct Game;

template <typename T, int SIZE> struct AtomicStack
{
  void push(const T& in) { stack[SDL_AtomicIncRef(&count)] = in; }
  T*   begin() { return stack; }
  T*   end() { return &stack[SDL_AtomicGet(&count)]; }
  void reset() { SDL_AtomicSet(&count, 0); }

  T            stack[SIZE];
  SDL_atomic_t count;
};

struct ThreadJobData
{
  int     thread_id;
  Engine& engine;
  Game&   game;
  Stack&  allocator;
};

struct Job
{
  const char* name;
  void (*execute)(ThreadJobData tjd);
};

struct Jobs : public ElementStack<Job, 128>
{
  void push(const char* name, void (*fcn)(ThreadJobData tjd))
  {
    Job job = {name, fcn};
    ElementStack<Job, 128>::push(job);
  }
};

struct JobSystem
{
  using JobFunction = void (*)(ThreadJobData tjd);

  // general controls
  bool thread_end_requested;

  // Workload dispatching
  Jobs         jobs;
  SDL_atomic_t jobs_taken;

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
      SDL_memcpy(thread_ids, other.thread_ids, count * sizeof(int));
      SDL_memcpy(duration_sec, other.duration_sec, count * sizeof(float));
      SDL_memcpy(job_names, other.job_names, count * sizeof(const char*));
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

struct Camera
{
  mat4x4 projection;
  mat4x4 view;
  vec3   position;
};

struct Cameras
{
  bool is_gameplay_bound() const { return &gameplay == current; }
  bool is_editor_bound() const { return &editor == current; }
  void bind_gameplay() { current = &gameplay; }
  void bind_editor() { current = &editor; }
  void toggle() { current = is_gameplay_bound() ? &editor : &gameplay; }

  Camera* current;
  Camera  gameplay; // behind the robot
  Camera  editor;   // birds eye
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
      VERTEX_BUFFER_CAPACITY_BYTES = 200 * 1024,
      INDEX_BUFFER_CAPACITY_BYTES  = 160 * 1024
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

  float update_times[200];
  float render_times[200];

  bool DEBUG_FLAG_1;
  bool DEBUG_FLAG_2;
  vec2 DEBUG_VEC2;
  vec2 DEBUG_VEC2_ADDITIONAL;
  vec4 DEBUG_LIGHT_ORTHO_PARAMS;

  bool lmb_clicked;
  int  lmb_last_cursor_position[2];
  int  lmb_current_cursor_position[2];

  //////////////////////////////////////////////////////////////////////////////
  // Gameplay logic
  //////////////////////////////////////////////////////////////////////////////

  vec2 vr_level_entry;
  vec2 vr_level_goal;
  vec3 light_source_position;

  Cameras cameras;

  vec3  player_position;
  vec3  player_velocity;
  vec3  player_acceleration;
  float camera_angle;
  float camera_updown_angle;
  bool  player_jumping;
  float player_jump_start_timestamp_sec;

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

  // gameplay mechanics
  float           booster_jet_fuel;
  WeaponSelection weapon_selections[2];

  SimpleEntity helmet_entity;
  SimpleEntity robot_entity;
  SimpleEntity box_entities[6];
  SimpleEntity matrioshka_entity;
  SimpleEntity monster_entity;
  SimpleEntity rigged_simple_entity;
  SimpleEntity axis_arrow_entities[3];

  //////////////////////////////////////////////////////////////////////////////
  // Editor logic
  //////////////////////////////////////////////////////////////////////////////

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
  bool     camera_relocation_in_progress;

  void startup(Engine& engine);
  void teardown(Engine& engine);
  void update(Engine& engine, float time_delta_since_last_frame_ms);
  void render(Engine& engine);
};
