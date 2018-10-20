#pragma once

#include "bitfield.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

VkDeviceSize align(VkDeviceSize unaligned, VkDeviceSize alignment);

struct ScheduledPipelineDestruction
{
  int        frame_countdown;
  VkPipeline pipeline;
};

struct GpuMemoryBlock
{
  VkDeviceMemory memory;
  VkDeviceSize   alignment;
  VkDeviceSize   stack_pointer;
};

#define GPU_DEVICE_LOCAL_MEMORY_POOL_SIZE (5 * 1024 * 1024)
#define GPU_HOST_VISIBLE_TRANSFER_SOURCE_MEMORY_POOL_SIZE (5 * 1024 * 1024)
#define GPU_HOST_COHERENT_MEMORY_POOL_SIZE (1 * 1024 * 1024)
#define GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE (500 * 1024 * 1024)
#define GPU_HOST_COHERENT_UBO_MEMORY_POOL_SIZE (1 * 1024 * 1024)
#define MEMORY_ALLOCATOR_POOL_SIZE (5 * 1024 * 1024)

struct DoubleEndedStack
{
  void* allocate_front(uint64_t size);
  void* allocate_back(uint64_t size);
  void  reset_back();

  uint8_t  memory[MEMORY_ALLOCATOR_POOL_SIZE];
  uint64_t stack_pointer_front;
  uint64_t stack_pointer_back;
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

struct Pipelines
{
  struct Coupling
  {
    VkPipeline       pipeline;
    VkPipelineLayout layout;
  };

  Coupling shadowmap;
  Coupling skybox;
  Coupling scene3D;
  Coupling pbr_water;
  Coupling colored_geometry;
  Coupling colored_geometry_triangle_strip;
  Coupling colored_geometry_skinned;
  Coupling green_gui;
  Coupling green_gui_weapon_selector_box_left;
  Coupling green_gui_weapon_selector_box_right;
  Coupling green_gui_lines;
  Coupling green_gui_sdf_font;
  Coupling green_gui_triangle;
  Coupling green_gui_radar_dots;
  Coupling imgui;
  Coupling debug_billboard;
  Coupling colored_model_wireframe;

  void destroy(VkDevice device);
};

struct RenderPasses
{
  template <uint32_t N> struct Coupling
  {
    VkRenderPass  render_pass;
    VkFramebuffer framebuffers[N];

    void destroy(VkDevice device)
    {
      vkDestroyRenderPass(device, render_pass, nullptr);
      for (VkFramebuffer& framebuffer : framebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    void begin(VkCommandBuffer cmd, uint32_t image_index)
    {
      VkCommandBufferInheritanceInfo inheritance = {
          .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
          .renderPass  = render_pass,
          .framebuffer = framebuffers[image_index],
      };

      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
          .pInheritanceInfo = &inheritance,
      };

      vkBeginCommandBuffer(cmd, &begin_info);
    }
  };

  Coupling<SHADOWMAP_CASCADE_COUNT> shadowmap;
  Coupling<SWAPCHAIN_IMAGES_COUNT>  skybox;
  Coupling<SWAPCHAIN_IMAGES_COUNT>  color_and_depth;
  Coupling<SWAPCHAIN_IMAGES_COUNT>  gui;

  void destroy(VkDevice device);
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

  // Used for vertex / index data which will be reused all the time
  GpuMemoryBlock gpu_device_local_memory_block;
  VkBuffer       gpu_device_local_memory_buffer;

  // Used for data transfers to device local memory
  GpuMemoryBlock gpu_host_visible_transfer_source_memory_block;
  VkBuffer       gpu_host_visible_transfer_source_memory_buffer;

  // Used for dynamic vertex/index data updates (for example imgui, dynamic draws)
  GpuMemoryBlock gpu_host_coherent_memory_block;
  VkBuffer       gpu_host_coherent_memory_buffer;

  // Image memory with images in use list
  GpuMemoryBlock gpu_device_images_memory_block;
  ImageResources image_resources;

  // Used for universal buffer objects
  GpuMemoryBlock gpu_host_coherent_ubo_memory_block;
  VkBuffer       gpu_host_coherent_ubo_memory_buffer;

  // General purpose allocator for application resources.
  // front : permanent allocations
  // back  : temporary allocations
  DoubleEndedStack allocator;


  VkDescriptorSetLayout shadow_pass_descriptor_set_layout;
  VkDescriptorSetLayout pbr_metallic_workflow_material_descriptor_set_layout;
  VkDescriptorSetLayout pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout;
  VkDescriptorSetLayout pbr_dynamic_lights_descriptor_set_layout;
  VkDescriptorSetLayout single_texture_in_frag_descriptor_set_layout;
  VkDescriptorSetLayout skinning_matrices_descriptor_set_layout;
  VkDescriptorSetLayout cascade_shadow_map_matrices_ubo_frag_set_layout;

  RenderPasses render_passes;
  Pipelines    pipelines;

  // Live shader reloading helpers.
  //
  // Each time pipeline is recreated the previous one has to be destroyed to release memory / gpu resources.
  // Unfortunately the pipeline can't be destroyed when in use, so the safest bet is to wait until any potential
  // command buffer finishes executing and then safely calling vkDestroyPipeline on the obsolete pipeline.
  // The list below stores both pipeline handles and the frame countdowns until the destruction can happen.
  ScheduledPipelineDestruction scheduled_pipelines_destruction[16];
  int                          scheduled_pipelines_destruction_count;

  void           startup();
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
