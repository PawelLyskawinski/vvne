#pragma once

#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

#define SWAPCHAIN_IMAGES_COUNT 2
#define MSAA_SAMPLE_COUNT VK_SAMPLE_COUNT_8_BIT

struct Engine
{
  struct GenericHandles
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
    VkImageView                depth_image_view;
    VkImage                    msaa_color_image;
    VkImageView                msaa_color_image_view;
    VkImage                    msaa_depth_image;
    VkImageView                msaa_depth_image_view;
    VkSemaphore                image_available;
    VkSemaphore                render_finished;
    VkSampler                  texture_sampler;
  } generic_handles;

  struct MemoryWithAlignment
  {
    VkDeviceSize allocate(VkDeviceSize size)
    {
      VkDeviceSize offset = used_memory;
      last_allocation     = (size % alignment) ? size + (alignment - (size % alignment)) : size;
      used_memory += last_allocation;
      return offset;
    }

    void pop()
    {
      used_memory -= last_allocation;
    }

    VkDeviceMemory memory;
    VkDeviceSize   alignment;
    VkDeviceSize   used_memory;
    VkDeviceSize   last_allocation;
  };

  struct GpuStaticGeometry : public MemoryWithAlignment
  {
    enum
    {
      MAX_MEMORY_SIZE_MB    = 5,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    VkBuffer buffer;
  } gpu_static_geometry;

  struct GpuStaticTransfer : public MemoryWithAlignment
  {
    enum
    {
      MAX_MEMORY_SIZE_MB    = 5,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    VkBuffer buffer;
  } gpu_static_transfer;

  struct GpuHostVisible : public MemoryWithAlignment
  {
    enum
    {
      MAX_MEMORY_SIZE_MB    = 1,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    VkBuffer buffer;
  } gpu_host_visible;

  struct Images : public MemoryWithAlignment
  {
    enum
    {
      MAX_COUNT             = 128,
      MAX_MEMORY_SIZE_MB    = 300,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    void add(VkImage image, VkImageView image_view)
    {
      images[loaded_count]      = image;
      image_views[loaded_count] = image_view;
      loaded_count += 1;
    }

    VkImage*     images;
    VkImageView* image_views;
    uint32_t     loaded_count;
  } images;

  struct UboHostVisible : MemoryWithAlignment
  {
    enum
    {
      MAX_MEMORY_SIZE_MB    = 1,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    VkBuffer buffer;
  } ubo_host_visible;

  struct SimpleRendering
  {
    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkFramebuffer         framebuffers[SWAPCHAIN_IMAGES_COUNT];

    enum Passes
    {
      Skybox = 0,
      Scene3D,
      ColoredGeometry,
      ColoredGeometrySkinned,
      ImGui,
      Count
    };

    VkPipelineLayout pipeline_layouts[Passes::Count];
    VkPipeline       pipelines[Passes::Count];
    VkCommandBuffer  secondary_command_buffers[SWAPCHAIN_IMAGES_COUNT * Passes::Count];
    VkCommandBuffer  primary_command_buffers[SWAPCHAIN_IMAGES_COUNT];
    VkFence          submition_fences[SWAPCHAIN_IMAGES_COUNT];
  } simple_rendering;

  struct DoubleEndedStack
  {
    enum
    {
      MAX_MEMORY_SIZE_MB    = 5,
      MAX_MEMORY_SIZE_KB    = MAX_MEMORY_SIZE_MB * 1024,
      MAX_MEMORY_SIZE_BYTES = MAX_MEMORY_SIZE_KB * 1024,
      MAX_MEMORY_SIZE       = MAX_MEMORY_SIZE_BYTES
    };

    int calculate_64bit_alignment_padding(int value)
    {
      return (value % 8) ? 8 - (value % 8) : 0;
    }

    template <typename T> T* allocate_front(int count = 1)
    {
      T*  result         = reinterpret_cast<T*>(&memory[front]);
      int size           = count * sizeof(T);
      int padding        = calculate_64bit_alignment_padding(size);
      int corrected_size = size + padding;
      front += corrected_size;
      return result;
    }

    template <typename T> T* allocate_back(int count = 1)
    {
      int size           = count * sizeof(T);
      int padding        = calculate_64bit_alignment_padding(size);
      int corrected_size = size + padding;
      back += corrected_size;
      return reinterpret_cast<T*>(&memory[MAX_MEMORY_SIZE - back]);
    }

    void reset_back()
    {
      back = 0;
    }

    uint8_t memory[MAX_MEMORY_SIZE];
    int     front;
    int     back;
  } double_ended_stack;

  void startup();
  void teardown();
  void print_memory_statistics();
  void submit_simple_rendering(uint32_t image_index);

  VkShaderModule load_shader(const char* file_path);

  int load_texture(const char* filepath);
  int load_texture_hdr(const char* filename);
  int load_texture(SDL_Surface* surface);

  // internals
private:
  void setup_simple_rendering();
};
