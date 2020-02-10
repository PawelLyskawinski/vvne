#pragma once

#include "engine/engine.hh"
#include "engine/gltf.hh"
#include "engine/math.hh"
#include "game_constants.hh"
#include "game_generate_gui_lines.hh"

struct LightSource
{
  Vec4 position;
  Vec4 color;
};

struct LightSourcesSoA
{
  Vec4 positions[64];
  Vec4 colors[64];
  int  count = 0;

  void push(const LightSource* begin, const LightSource* end);
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

#define LUCIDA_SANS_SDF_CHARS_COUNT 97

struct Materials
{
  uint8_t lucida_sans_sdf_char_ids[LUCIDA_SANS_SDF_CHARS_COUNT];
  SdfChar lucida_sans_sdf_chars[LUCIDA_SANS_SDF_CHARS_COUNT];
  Texture lucida_sans_sdf_image;

  Texture      imgui_font_texture;
  VkDeviceSize imgui_vertex_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
  VkDeviceSize imgui_index_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];

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
  VkDescriptorSet frustum_planes_dset[SWAPCHAIN_IMAGES_COUNT];

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
  VkDeviceSize frustum_planes_ubo_offsets[SWAPCHAIN_IMAGES_COUNT];

  // cascade shadow mapping
  Mat4x4 cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT];
  float  cascade_split_depths[SHADOWMAP_CASCADE_COUNT];

  // CSM debuging mostly, but can be used as a billboard space in any shader
  VkDeviceSize green_gui_billboard_vertex_buffer_offset;
  VkDeviceSize regular_billboard_vertex_buffer_offset;

  // frame cache
  SDL_mutex*      pbr_light_sources_cache_lock;
  LightSourcesSoA pbr_light_sources_cache;
  Vec2            gui_lines_memory_cache[MAX_ROBOT_GUI_LINES];

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

  Vec3         light_source_position;
  VkDeviceSize vr_level_vertex_buffer_offset;
  VkDeviceSize vr_level_index_buffer_offset;
  int          vr_level_index_count;
  VkIndexType  vr_level_index_type;
  VkDeviceSize tesselation_vb_offset;
  uint32_t     tesselation_instances;

  VkDeviceSize     green_gui_rulers_buffer_offsets[SWAPCHAIN_IMAGES_COUNT];
  GuiLineSizeCount gui_green_lines_count;
  GuiLineSizeCount gui_red_lines_count;
  GuiLineSizeCount gui_yellow_lines_count;

  void setup(Engine& engine);
  void teardown(Engine& engine);
};
