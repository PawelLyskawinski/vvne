#pragma once

#include "bitfield.hh"
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

VkDeviceSize align(VkDeviceSize unaligned, VkDeviceSize alignment);
int          find_first_zeroed_bit_offset(uint64_t bitmap);

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

struct Engine
{
  // configuration
  static constexpr int  SWAPCHAIN_IMAGES_COUNT  = 2;
  static constexpr int  SHADOWMAP_IMAGE_DIM     = 1024 * 2;
  static constexpr int  SHADOWMAP_CASCADE_COUNT = 4;
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
  VkImage                    shadowmap_images[SWAPCHAIN_IMAGES_COUNT];
  VkImageView                shadowmap_image_views[SWAPCHAIN_IMAGES_COUNT];
  VkImageView                shadowmap_cascade_image_views[SHADOWMAP_CASCADE_COUNT * SWAPCHAIN_IMAGES_COUNT];

  //
  // Used for vertex / index data which will be reused all the time
  //
  GpuMemoryBlock gpu_device_local_memory_block;
  VkBuffer       gpu_device_local_memory_buffer;

  //
  // Used for data transfers to device local memory
  //
  GpuMemoryBlock gpu_host_visible_transfer_source_memory_block;
  VkBuffer       gpu_host_visible_transfer_source_memory_buffer;

  //
  // Used for dynamic vertex/index data updates (for example imgui, dynamic draws)
  //
  GpuMemoryBlock gpu_host_coherent_memory_block;
  VkBuffer       gpu_host_coherent_memory_buffer;

  //
  // Image memory with images in use list
  //
  GpuMemoryBlock gpu_device_images_memory_block;
  ImageResources image_resources;

  //
  // Used for universal buffer objects
  //
  GpuMemoryBlock gpu_host_coherent_ubo_memory_block;
  VkBuffer       gpu_host_coherent_ubo_memory_buffer;

  //
  // General purpose allocator for application resources.
  // front : permanent allocations
  // back  : temporary allocations
  //
  DoubleEndedStack allocator;

  struct ShadowMapping
  {
    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipeline            pipeline;
    VkPipelineLayout      pipeline_layout;
    VkFramebuffer         framebuffers[SHADOWMAP_CASCADE_COUNT * SWAPCHAIN_IMAGES_COUNT];
    VkSampler             sampler;
  } shadow_mapping;

  struct SimpleRendering
  {
    VkRenderPass render_pass;

    VkDescriptorSetLayout pbr_metallic_workflow_material_descriptor_set_layout;
    VkDescriptorSetLayout pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout;
    VkDescriptorSetLayout pbr_dynamic_lights_descriptor_set_layout;
    VkDescriptorSetLayout single_texture_in_frag_descriptor_set_layout;
    VkDescriptorSetLayout skinning_matrices_descriptor_set_layout;
    VkDescriptorSetLayout cascade_shadow_map_matrices_ubo_frag_set_layout;

    VkFramebuffer framebuffers[SWAPCHAIN_IMAGES_COUNT];

    struct Pass
    {
      enum
      {
        Skybox,
        Objects3D,
        RobotGui,
        RadarDots,
        ImGui,
        Count
      };
    };

    struct Pipeline
    {
      enum
      {
        Skybox,
        Scene3D,
        PbrWater,
        ColoredGeometry,
        ColoredGeometryTriangleStrip,
        ColoredGeometrySkinned,
        GreenGui,
        GreenGuiWeaponSelectorBoxLeft,
        GreenGuiWeaponSelectorBoxRight,
        GreenGuiLines,
        GreenGuiSdfFont,
        GreenGuiTriangle,
        GreenGuiRadarDots,
        ImGui,
        DebugBillboard,
        Count
      };
    };

    VkPipelineLayout pipeline_layouts[Pipeline::Count];
    VkPipeline       pipelines[Pipeline::Count];
    VkCommandBuffer  primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
    VkFence          submition_fences[SWAPCHAIN_IMAGES_COUNT];
  } simple_rendering;

  void startup();
  void teardown();

  VkShaderModule load_shader(const char* file_path);

  Texture load_texture(const char* filepath);
  Texture load_texture_hdr(const char* filename);
  Texture load_texture(SDL_Surface* surface);

  //
  // Live shader reloading helpers.
  //
  // Each time pipeline is recreated the previous one has to be destroyed to release memory / gpu resources.
  // Unfortunately the pipeline can't be destroyed when in use, so the safest bet is to wait until any potential
  // command buffer finishes executing and then safely calling vkDestroyPipeline on the obsolete pipeline.
  // The list below stores both pipeline handles and the frame countdowns until the destruction can happen.
  //
  ScheduledPipelineDestruction scheduled_pipelines_destruction[16];
  int                          scheduled_pipelines_destruction_count;

private:
  void setup_shadow_mapping();
  void setup_simple_rendering();
};
