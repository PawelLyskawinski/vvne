#pragma once

#include "bitfield.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

constexpr uint32_t operator"" _KB(unsigned long long in) { return 1024u * static_cast<uint32_t>(in); }
constexpr uint32_t operator"" _MB(unsigned long long in) { return 1024u * 1024u * static_cast<uint32_t>(in); }
constexpr uint32_t GPU_DEVICE_LOCAL_MEMORY_POOL_SIZE                 = 5_MB;
constexpr uint32_t GPU_HOST_VISIBLE_TRANSFER_SOURCE_MEMORY_POOL_SIZE = 5_MB;
constexpr uint32_t GPU_HOST_COHERENT_MEMORY_POOL_SIZE                = 1_MB;
constexpr uint32_t GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE           = 500_MB;
constexpr uint32_t GPU_HOST_COHERENT_UBO_MEMORY_POOL_SIZE            = 1_MB;
constexpr uint32_t HOST_PERMANENT_ALLOCATOR_POOL_SIZE                = 3_MB;
constexpr uint32_t HOST_DIRTY_ALLOCATOR_POOL_SIZE                    = 5_MB;

template <typename T> T align(T unaligned, T alignment)
{
  T result = unaligned;
  if (unaligned % alignment)
    result = unaligned + alignment - (unaligned % alignment);
  return result;
}

struct Stack
{
  void setup(uint64_t capacity)
  {
    data = reinterpret_cast<uint8_t*>(SDL_malloc(capacity));
    sp   = 0;
  }

  template <typename T> T* alloc(int count = 1)
  {
    T* r = reinterpret_cast<T*>(&data[sp]);
    sp += align<uint64_t>(count * sizeof(T), 8);
    return r;
  }

  void reset() { sp = 0; }
  void teardown() { SDL_free(data); }

  uint8_t* data;
  uint64_t sp;
};

struct Texture
{
  int image_idx;
  int image_view_idx;
};

class ImageResources
{
public:
  int add(VkImage image);
  int add(VkImageView image);

  static constexpr int image_capacity      = 64;
  static constexpr int image_view_capacity = 64;

  ComponentBitfield images_bitmap;
  ComponentBitfield image_views_bitmap;

  VkImage     images[image_capacity];
  VkImageView image_views[image_view_capacity];
};

constexpr int SWAPCHAIN_IMAGES_COUNT  = 2;
constexpr int SHADOWMAP_IMAGE_DIM     = 1024 * 2;
constexpr int SHADOWMAP_CASCADE_COUNT = 4;

struct PipelineWithHayout
{
  VkPipeline       pipeline;
  VkPipelineLayout layout;
};

struct Pipelines
{
  void destroy(VkDevice device);

  using Pair = PipelineWithHayout;

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
};

struct RenderPass
{
  void destroy(VkDevice device);
  void begin(VkCommandBuffer cmd, uint32_t image_index);

  VkRenderPass   render_pass;
  VkFramebuffer* framebuffers;
  uint32_t       framebuffers_count;
};

struct RenderPasses
{
  void init();
  void destroy(VkDevice device);

  RenderPass shadowmap;
  RenderPass skybox;
  RenderPass color_and_depth;
  RenderPass gui;

  VkFramebuffer shadowmap_framebuffers[SHADOWMAP_CASCADE_COUNT];
  VkFramebuffer skybox_framebuffers[SWAPCHAIN_IMAGES_COUNT];
  VkFramebuffer color_and_depth_framebuffers[SWAPCHAIN_IMAGES_COUNT];
  VkFramebuffer gui_framebuffers[SWAPCHAIN_IMAGES_COUNT];
};

struct DescriptorSetLayouts
{
  void destroy(VkDevice device);

  VkDescriptorSetLayout shadow_pass;
  VkDescriptorSetLayout pbr_metallic_workflow_material;
  VkDescriptorSetLayout pbr_ibl_cubemaps_and_brdf_lut;
  VkDescriptorSetLayout pbr_dynamic_lights;
  VkDescriptorSetLayout single_texture_in_frag;
  VkDescriptorSetLayout skinning_matrices;
  VkDescriptorSetLayout cascade_shadow_map_matrices_ubo_frag;
};

struct GpuMemoryBlock
{
  VkDeviceMemory memory;
  VkDeviceSize   alignment;
  VkDeviceSize   stack_pointer;
};

struct MemoryBlocks
{
  void destroy(VkDevice device);

  GpuMemoryBlock device_local;
  GpuMemoryBlock host_visible_transfer_source;
  GpuMemoryBlock device_images;
  GpuMemoryBlock host_coherent;
  GpuMemoryBlock host_coherent_ubo;
};

struct Engine
{
  // configuration
  VkSampleCountFlagBits MSAA_SAMPLE_COUNT;

  // data
  VkInstance                 instance;
  VkDebugReportCallbackEXT   debug_callback;
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
  VkImage                    msaa_color_image;
  VkImageView                msaa_color_image_view;
  VkImage                    depth_image;
  VkImageView                depth_image_view;
  VkSemaphore                image_available;
  VkSemaphore                render_finished;
  VkSampler                  texture_sampler;
  VkSampler                  shadowmap_sampler;
  VkImage                    shadowmap_image;
  VkImageView                shadowmap_image_view;
  VkImageView                shadowmap_cascade_image_views[SHADOWMAP_CASCADE_COUNT];
  VkFence                    submition_fences[SWAPCHAIN_IMAGES_COUNT];

  MemoryBlocks memory_blocks;

  VkBuffer gpu_device_local_memory_buffer; // Used for vertex / index data which will be reused all the time
  VkBuffer gpu_host_visible_transfer_source_memory_buffer; // Used for data transfers to device local memory
  VkBuffer
                 gpu_host_coherent_memory_buffer; // Used for dynamic vertex/index data updates (for example imgui, dynamic draws)
  ImageResources image_resources;                 // Image memory with images in use list
  VkBuffer       gpu_host_coherent_ubo_memory_buffer; // Used for universal buffer objects

  Stack permanent_stack;
  Stack dirty_stack;

  DescriptorSetLayouts descriptor_set_layouts;
  RenderPasses         render_passes;
  Pipelines            pipelines;

  void           startup(bool vulkan_validation_enabled);
  void           teardown();
  VkShaderModule load_shader(const char* file_path);
  Texture        load_texture(const char* filepath);
  Texture        load_texture_hdr(const char* filename);
  Texture        load_texture(SDL_Surface* surface);

private:
  void setup_render_passes();
  void setup_framebuffers();
  void setup_descriptor_set_layouts();
  void setup_pipeline_layouts();
  void setup_pipelines();
};
