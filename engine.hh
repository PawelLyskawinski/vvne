#pragma once

#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

#define SWAPCHAIN_IMAGES_COUNT 2

struct Engine
{
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
  VkImage                    depth_image;
  VkDeviceMemory             depth_image_memory;
  VkImageView                depth_image_view;
  VkSemaphore                image_available;
  VkSemaphore                render_finished;
  VkSampler                  texture_samplers[SWAPCHAIN_IMAGES_COUNT];

  uint32_t       loaded_textures;
  VkDeviceMemory images_memory[128];
  VkImage        images[128];
  VkImageView    image_views[128];

  struct SimpleRenderer
  {
    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layouts[4 * SWAPCHAIN_IMAGES_COUNT];
    VkDescriptorSet       descriptor_sets[4 * SWAPCHAIN_IMAGES_COUNT];
    VkFramebuffer         framebuffers[SWAPCHAIN_IMAGES_COUNT];

    VkPipelineLayout pipeline_layouts[2];
    VkPipeline       pipelines[2];

    struct GuiResources
    {
      VkDeviceMemory  vertex_memory[SWAPCHAIN_IMAGES_COUNT];
      VkBuffer        vertex_buffers[SWAPCHAIN_IMAGES_COUNT];
      VkDeviceMemory  index_memory[SWAPCHAIN_IMAGES_COUNT];
      VkBuffer        index_buffers[SWAPCHAIN_IMAGES_COUNT];
      VkCommandBuffer secondary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
    };

    struct SceneResources
    {
      VkDeviceMemory  cube_buffer_memory;
      VkBuffer        cube_buffer;
      VkCommandBuffer secondary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
    };

    GuiResources   gui;
    SceneResources scene;

    VkCommandBuffer primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
    VkFence         submition_fences[SWAPCHAIN_IMAGES_COUNT];
  };

  SimpleRenderer simple_renderer;
};

void load_vulkan_functions();
void engine_basic_startup(Engine& engine);
void engine_renderer_simple(Engine& engine);
void engine_teardown(Engine& engine);

// etc
uint32_t       find_memory_type_index(VkPhysicalDeviceMemoryProperties* properties, VkMemoryRequirements* reqs,
                                      VkMemoryPropertyFlags searched);
VkShaderModule engine_load_shader(Engine& engine, const char* filepath);
int            engine_load_texture(Engine& engine, const char* filepath);
int            engine_load_texture(Engine& engine, SDL_Surface* surface);

// structure definitions
struct CubeBuffer
{
  uint32_t indices[6 * 6];

  struct Vertex
  {
    float position[3];
    float normal[3];
    float tex_coord[2];
  } vertices[6 * 4];
};
