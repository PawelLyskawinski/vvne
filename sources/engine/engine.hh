#pragma once

#include "allocators.hh"
#include "engine_constants.hh"
#include "free_list_allocator.hh"
#include "gpu_memory_allocator.hh"
#include "job_system.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

constexpr uint32_t operator"" _KB(unsigned long long in) { return 1024u * static_cast<uint32_t>(in); }
constexpr uint32_t operator"" _MB(unsigned long long in) { return 1024u * 1024u * static_cast<uint32_t>(in); }

struct Pipelines
{
  struct Pair
  {
    VkPipeline       pipeline;
    VkPipelineLayout layout;
  };

  Pair shadowmap;
  Pair skybox;
  Pair scene3D;
  Pair pbr_water;
  Pair colored_geometry;
  Pair colored_geometry_triangle_strip;
  Pair colored_geometry_skinned;
  Pair green_gui;
  Pair green_gui_weapon_selector_box_left;
  Pair green_gui_weapon_selector_box_right;
  Pair green_gui_lines;
  Pair green_gui_sdf_font;
  Pair green_gui_triangle;
  Pair green_gui_radar_dots;
  Pair imgui;
  Pair debug_billboard;
  Pair colored_model_wireframe;
  Pair tesselated_ground;
};

struct RenderPass
{
  VkRenderPass   render_pass;
  VkFramebuffer* framebuffers;
  uint32_t       framebuffers_count;

  void begin(VkCommandBuffer cmd, uint32_t image_index) const;
};

struct RenderPasses
{
  RenderPass shadowmap;
  RenderPass skybox;
  RenderPass color_and_depth;
  RenderPass gui;
};

struct DescriptorSetLayouts
{
  VkDescriptorSetLayout shadow_pass;
  VkDescriptorSetLayout pbr_metallic_workflow_material;
  VkDescriptorSetLayout pbr_ibl_cubemaps_and_brdf_lut;
  VkDescriptorSetLayout pbr_dynamic_lights;
  VkDescriptorSetLayout single_texture_in_frag;
  VkDescriptorSetLayout skinning_matrices;
  VkDescriptorSetLayout cascade_shadow_map_matrices_ubo_frag;
  VkDescriptorSetLayout frustum_planes;
};

struct GpuMemoryBlock
{
  VkDeviceMemory     memory;
  VkDeviceSize       alignment;
  GpuMemoryAllocator allocator;

  VkDeviceSize allocate_aligned(VkDeviceSize size);
};

struct MemoryBlocks
{
  GpuMemoryBlock device_local;
  GpuMemoryBlock host_visible_transfer_source;
  GpuMemoryBlock device_images;
  GpuMemoryBlock host_coherent;
  GpuMemoryBlock host_coherent_ubo;
};

struct Texture
{
  VkImage      image;
  VkImageView  image_view;
  VkDeviceSize memory_offset;
};

struct Engine
{
  // configuration
  VkSampleCountFlagBits MSAA_SAMPLE_COUNT;

  // renderdoc support
  bool                              renderdoc_marker_naming_enabled;
  PFN_vkDebugMarkerSetObjectTagEXT  vkDebugMarkerSetObjectTag;
  PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName;
  PFN_vkCmdDebugMarkerBeginEXT      vkCmdDebugMarkerBegin;
  PFN_vkCmdDebugMarkerEndEXT        vkCmdDebugMarkerEnd;
  PFN_vkCmdDebugMarkerInsertEXT     vkCmdDebugMarkerInsert;

  // data
  VkInstance                 instance;
  VkDebugUtilsMessengerEXT   debug_callback;
  SDL_Window*                window;
  VkPhysicalDevice           physical_device;
  VkPhysicalDeviceProperties physical_device_properties;
  VkSurfaceKHR               surface;
  VkSurfaceCapabilitiesKHR   surface_capabilities;
  VkExtent2D                 extent2D;
  uint32_t                   graphics_family_index;
  VkDevice                   device;
  VkQueue                    graphics_queue;
  VkSurfaceFormatKHR         surface_format;
  VkPresentModeKHR           present_mode;
  VkSwapchainKHR             swapchain;
  VkImage                    swapchain_images[SWAPCHAIN_IMAGES_COUNT];
  VkImageView                swapchain_image_views[SWAPCHAIN_IMAGES_COUNT];
  VkCommandPool              graphics_command_pool;
  VkDescriptorPool           descriptor_pool;
  Texture                    msaa_color_image;
  Texture                    depth_image;
  VkSemaphore                image_available;
  VkSemaphore                render_finished;
  VkSampler                  texture_sampler;
  VkSampler                  shadowmap_sampler;
  Texture                    shadowmap_image;
  VkImageView                shadowmap_cascade_image_views[SHADOWMAP_CASCADE_COUNT];
  VkFence                    submition_fences[SWAPCHAIN_IMAGES_COUNT];

  MemoryBlocks memory_blocks;
  SDL_mutex*   host_coherent_ubo_lock;

  // Used for vertex / index data which will be reused all the time
  VkBuffer gpu_device_local_memory_buffer;

  // Used for data transfers to device local memory
  VkBuffer gpu_host_visible_transfer_source_memory_buffer;

  // Used for dynamic vertex/index data updates (for example imgui, dynamic draws)
  VkBuffer gpu_host_coherent_memory_buffer;

  // Lazy "to by removed at the end of program" lists.
  ElementStack<VkImage>     autoclean_images;
  ElementStack<VkImageView> autoclean_image_views;

  // Used for universal buffer objects
  VkBuffer gpu_host_coherent_ubo_memory_buffer;

  DescriptorSetLayouts descriptor_set_layouts;
  RenderPasses         render_passes;
  Pipelines            pipelines;

  FreeListAllocator generic_allocator;
  JobSystem         job_system;

  void           startup(bool vulkan_validation_enabled);
  void           teardown();
  void           change_resolution(VkExtent2D new_size);
  VkShaderModule load_shader(const char* file_path);
  Texture        load_texture_hdr(const char* filename);
  Texture        load_texture(const char* filepath, bool register_for_destruction = true);
  Texture        load_texture(SDL_Surface* surface, bool register_for_destruction = true);

private:
  void setup_render_passes();
  void setup_framebuffers();
  void setup_descriptor_set_layouts();
  void setup_pipeline_layouts();
  void setup_pipelines();
};
