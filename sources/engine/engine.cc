#include "engine.hh"
#include "math.hh"
#include "sha256.h"
#include "vulkan_generic.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <SDL2/SDL_timer.h>
#include <stb_image.h>
#pragma GCC diagnostic pop

namespace {

const uint32_t initial_window_width                              = 1900;
const uint32_t initial_window_height                             = 1200;
const uint32_t gpu_device_local_memory_pool_size                 = 5_MB;
const uint32_t gpu_host_visible_transfer_source_memory_pool_size = 5_MB;
const uint32_t gpu_host_coherent_memory_pool_size                = 1_MB;
const uint32_t gpu_device_local_image_memory_pool_size           = 500_MB;
const uint32_t gpu_host_coherent_ubo_memory_pool_size            = 1_MB;

uint32_t find_memory_type_index(VkPhysicalDeviceMemoryProperties* properties, VkMemoryRequirements* reqs,
                                VkMemoryPropertyFlags searched)
{
  for (uint32_t i = 0; i < properties->memoryTypeCount; ++i)
  {
    if (0 == (reqs->memoryTypeBits & (1 << i)))
      continue;

    VkMemoryPropertyFlags memory_type_properties = properties->memoryTypes[i].propertyFlags;

    if (searched == (memory_type_properties & searched))
      return i;
  }

  // this code fragment should never be reached!
  SDL_assert(false);
  return 0;
}

void renderpass_allocate_memory(HierarchicalAllocator& a, RenderPass& rp, uint32_t n)
{
  rp.framebuffers_count = n;
  rp.framebuffers       = a.allocate<VkFramebuffer>(n);
}

VkComponentMapping gen_rgba_cm()
{
  VkComponentMapping cm = {};

  cm.r = VK_COMPONENT_SWIZZLE_R;
  cm.g = VK_COMPONENT_SWIZZLE_G;
  cm.b = VK_COMPONENT_SWIZZLE_B;
  cm.a = VK_COMPONENT_SWIZZLE_A;

  return cm;
}

void allocate_memory_for_image(VkDevice device, Texture& t, GpuMemoryBlock& block)
{
  VkMemoryRequirements reqs = {};
  vkGetImageMemoryRequirements(device, t.image, &reqs);
  t.memory_offset = block.allocator.allocate_bytes(align(reqs.size, reqs.alignment));
  vkBindImageMemory(device, t.image, block.memory, t.memory_offset);
}

} // namespace

VkDeviceSize GpuMemoryBlock::allocate_aligned(VkDeviceSize size)
{
  return allocator.allocate_bytes(align(size, alignment));
}

void GpuMemoryBlock::allocate_aligned_ranged(VkDeviceSize dst[], const uint32_t count, const VkDeviceSize size)
{
  for (uint32_t i = 0; i < count; ++i)
  {
    dst[i] = allocate_aligned(size);
  }
}

void Engine::startup(bool vulkan_validation_enabled)
{
  // generic_allocator.init();

  renderpass_allocate_memory(*generic_allocator, render_passes.shadowmap, SHADOWMAP_CASCADE_COUNT);
  renderpass_allocate_memory(*generic_allocator, render_passes.skybox, SWAPCHAIN_IMAGES_COUNT);
  renderpass_allocate_memory(*generic_allocator, render_passes.color_and_depth, SWAPCHAIN_IMAGES_COUNT);
  renderpass_allocate_memory(*generic_allocator, render_passes.gui, SWAPCHAIN_IMAGES_COUNT);

  window = SDL_CreateWindow("vvne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, initial_window_width,
                            initial_window_height, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

  {
    InstanceConf conf = {
        .validation = vulkan_validation_enabled ? RuntimeValidation::Enabled : RuntimeValidation::Disabled,
        .name       = "vvne",
        .window     = window,
    };
    instance = CreateInstance(conf, *generic_allocator);
  }

  if (vulkan_validation_enabled)
  {
    debug_callback = CreateDebugUtilsMessenger(instance);
  }

  physical_device = SelectPhysicalDevice(instance, PhysicalDeviceSelectionStrategy::SelectFirst, *generic_allocator);
  vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
  SDL_Log("Selecting graphics card: %s", physical_device_properties.deviceName);

  SDL_bool surface_result = SDL_Vulkan_CreateSurface(window, instance, &surface);
  if (SDL_FALSE == surface_result)
  {
    SDL_Log("%s", SDL_GetError());
    return;
  }

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);
  extent2D                        = surface_capabilities.currentExtent;
  graphics_family_index           = SelectGraphicsFamilyIndex(physical_device, surface, *generic_allocator);
  renderdoc_marker_naming_enabled = IsRenderdocSupported(physical_device, *generic_allocator);

  {
    DeviceConf conf = {
        .physical_device       = physical_device,
        .graphics_family_index = graphics_family_index,
        .validation            = vulkan_validation_enabled ? RuntimeValidation::Enabled : RuntimeValidation::Disabled,
        .renderdoc_extension_active = renderdoc_marker_naming_enabled,
    };
    device = CreateDevice(conf, *generic_allocator);
  }

  if (renderdoc_marker_naming_enabled)
  {
    RenderdocFunctions fcns = LoadRenderdocFunctions(device);

    vkDebugMarkerSetObjectTag  = fcns.set_object_tag;
    vkDebugMarkerSetObjectName = fcns.set_object_name;
    vkCmdDebugMarkerBegin      = fcns.begin;
    vkCmdDebugMarkerEnd        = fcns.end;
    vkCmdDebugMarkerInsert     = fcns.insert;
  }

  vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);
  job_system.setup(device, graphics_family_index);

  surface_format = SelectSurfaceFormat(physical_device, surface,
                                       SurfaceFormatSelectionStrategy::PreferSRGBnonlinearBGRA8, *generic_allocator);
  present_mode =
      SelectPresentMode(physical_device, surface, PresentModeSelectionStrategy::PreferImmediate, *generic_allocator);

  {
    SwapchainConf conf = {
        .surface        = surface,
        .surface_format = surface_format,
        .extent         = extent2D,
        .transform      = surface_capabilities.currentTransform,
        .present_mode   = present_mode,
        .count          = SWAPCHAIN_IMAGES_COUNT,
    };
    swapchain = CreateSwapchain(device, conf);

    uint32_t swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, nullptr);
    SDL_assert(SWAPCHAIN_IMAGES_COUNT == swapchain_images_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, swapchain_images);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      VkImageViewCreateInfo ci = {
          .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image            = swapchain_images[i],
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = surface_format.format,
          .subresourceRange = sr,
      };

      vkCreateImageView(device, &ci, nullptr, &swapchain_image_views[i]);
    }
  }

  {
    VkCommandPoolCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_family_index,
    };

    vkCreateCommandPool(device, &ci, nullptr, &graphics_command_pool);
  }

  // Pool sizes below are just an suggestions. They have to be adjusted for the final release builds
  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 * SWAPCHAIN_IMAGES_COUNT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 * SWAPCHAIN_IMAGES_COUNT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20 * SWAPCHAIN_IMAGES_COUNT},
    };

    VkDescriptorPoolCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 60 * SWAPCHAIN_IMAGES_COUNT,
        .poolSizeCount = SDL_arraysize(pool_sizes),
        .pPoolSizes    = pool_sizes,
    };

    vkCreateDescriptorPool(device, &ci, nullptr, &descriptor_pool);
  }

  {
    VkSemaphoreCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device, &ci, nullptr, &image_available);
    vkCreateSemaphore(device, &ci, nullptr, &render_finished);
  }

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    ImageConf conf = {
        .type         = ImageType::MSAAResolve,
        .format       = surface_format.format,
        .extent       = extent2D,
        .sample_count = MSAA_SAMPLE_COUNT,
    };
    msaa_color_image.image = CreateImage(device, conf);
  }

  {
    ImageConf conf = {
        .type         = ImageType::DepthTest,
        .extent       = extent2D,
        .sample_count = MSAA_SAMPLE_COUNT,
    };
    depth_image.image = CreateImage(device, conf);
  }

  {
    ImageConf conf = {
        .type   = ImageType::CascadeShadowMap,
        .extent = {SHADOWMAP_IMAGE_DIM, SHADOWMAP_IMAGE_DIM},
        .layers = SHADOWMAP_CASCADE_COUNT,
    };
    shadowmap_image.image = CreateImage(device, conf);
  }

  texture_sampler   = CreateSampler(device, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
  shadowmap_sampler = CreateSampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

  // STATIC_GEOMETRY

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = gpu_device_local_memory_pool_size,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ci.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    vkCreateBuffer(device, &ci, nullptr, &gpu_device_local_memory_buffer);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, gpu_device_local_memory_buffer, &reqs);
    memory_blocks.device_local.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    memory_blocks.device_local.allocator.init(reqs.size);
    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.device_local.memory);
    vkBindBufferMemory(device, gpu_device_local_memory_buffer, memory_blocks.device_local.memory, 0);
  }

  // STATIC_GEOMETRY_TRANSFER
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = gpu_host_visible_transfer_source_memory_pool_size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(device, &ci, nullptr, &gpu_host_visible_transfer_source_memory_buffer);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, gpu_host_visible_transfer_source_memory_buffer, &reqs);
    memory_blocks.host_visible_transfer_source.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(
            &properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    memory_blocks.host_visible_transfer_source.allocator.init(reqs.size);
    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.host_visible_transfer_source.memory);
    vkBindBufferMemory(device, gpu_host_visible_transfer_source_memory_buffer,
                       memory_blocks.host_visible_transfer_source.memory, 0);
  }

  // HOST VISIBLE

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = gpu_host_coherent_memory_pool_size,
        .usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(device, &ci, nullptr, &gpu_host_coherent_memory_buffer);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, gpu_host_coherent_memory_buffer, &reqs);
    memory_blocks.host_coherent.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
    };

    memory_blocks.host_coherent.allocator.init(reqs.size);
    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.host_coherent.memory);
    vkBindBufferMemory(device, gpu_host_coherent_memory_buffer, memory_blocks.host_coherent.memory, 0);
  }

  // IMAGES
  {
    memory_blocks.device_images.allocator.init(gpu_device_local_image_memory_pool_size);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, depth_image.image, &reqs);

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = gpu_device_local_image_memory_pool_size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.device_images.memory);
  }

  allocate_memory_for_image(device, depth_image, memory_blocks.device_images);
  allocate_memory_for_image(device, shadowmap_image, memory_blocks.device_images);
  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    allocate_memory_for_image(device, msaa_color_image, memory_blocks.device_images);
  }

  // image views can only be created when memory is bound to the image handle

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = msaa_color_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = surface_format.format,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &msaa_color_image.image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = depth_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &depth_image.image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = SHADOWMAP_CASCADE_COUNT,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = shadowmap_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &shadowmap_image.image_view);
  }

  for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount     = 1,
        .baseArrayLayer = static_cast<uint32_t>(cascade_idx),
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = shadowmap_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &shadowmap_cascade_image_views[cascade_idx]);
  }

  {
    autoclean_images.push(shadowmap_image.image);
    autoclean_image_views.push(shadowmap_image.image_view);

    for (auto& image_view : shadowmap_cascade_image_views)
      autoclean_image_views.push(image_view);
  }

  // UBO HOST VISIBLE
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = gpu_host_coherent_ubo_memory_pool_size,
        .usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(device, &ci, nullptr, &gpu_host_coherent_ubo_memory_buffer);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, gpu_host_coherent_ubo_memory_buffer, &reqs);
    memory_blocks.host_coherent_ubo.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(
            &properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    memory_blocks.host_coherent_ubo.allocator.init(reqs.size);
    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.host_coherent_ubo.memory);
    vkBindBufferMemory(device, gpu_host_coherent_ubo_memory_buffer, memory_blocks.host_coherent_ubo.memory, 0);
  }

  //
  // Image layout transitions
  //
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo info = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };
      vkAllocateCommandBuffers(device, &info, &cmd);
    }

    {
      VkCommandBufferBeginInfo info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };
      vkBeginCommandBuffer(cmd, &info);
    }

    {
      VkImageMemoryBarrier barriers[] = {
          // shadow map
          {
              .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .srcAccessMask       = 0,
              .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
              .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
              .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image               = shadowmap_image.image,
              .subresourceRange =
                  {
                      .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                      .baseMipLevel   = 0,
                      .levelCount     = 1,
                      .baseArrayLayer = 0,
                      .layerCount     = SHADOWMAP_CASCADE_COUNT,
                  },
          },
          // depth test attachment
          {
              .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .srcAccessMask = 0,
              .dstAccessMask =
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
              .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image               = depth_image.image,
              .subresourceRange =
                  {
                      .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                      .baseMipLevel   = 0,
                      .levelCount     = 1,
                      .baseArrayLayer = 0,
                      .layerCount     = 1,
                  },
          },
      };

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &barriers[0]);
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &barriers[1]);

      vkEndCommandBuffer(cmd);

      {
        VkSubmitInfo info = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &cmd,
        };
        vkQueueSubmit(graphics_queue, 1, &info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);
      }

      vkFreeCommandBuffers(device, graphics_command_pool, 1, &cmd);
    }
  }

  setup_render_passes();
  setup_framebuffers();
  setup_descriptor_set_layouts();
  setup_pipeline_layouts();

  {
    uint64_t a = SDL_GetTicks();
    setup_pipelines();
    SDL_Log("setup_pipelines took %ums", static_cast<uint32_t>(SDL_GetTicks() - a));
  }

  for (VkFence& submition_fence : submition_fences)
  {
    VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    vkCreateFence(device, &ci, nullptr, &submition_fence);
  }
}

namespace {

template <typename TElement> class StructureAsArrayView
{
public:
  template <typename TOriginal>
  explicit StructureAsArrayView(const TOriginal* orig)
      : m_begin(reinterpret_cast<const TElement*>(orig))
      , m_end(m_begin + (sizeof(TOriginal) / sizeof(TElement)))
  {
  }

  [[nodiscard]] const TElement* begin() const
  {
    return m_begin;
  }
  [[nodiscard]] const TElement* end() const
  {
    return m_end;
  }

private:
  const TElement* m_begin;
  const TElement* m_end;
};

} // namespace

void Engine::teardown()
{
  vkDeviceWaitIdle(device);
  job_system.teardown(device);

  for (const VkDescriptorSetLayout& it : StructureAsArrayView<VkDescriptorSetLayout>(&descriptor_set_layouts))
  {
    vkDestroyDescriptorSetLayout(device, it, nullptr);
  }

  for (const RenderPass& it : StructureAsArrayView<RenderPass>(&render_passes))
  {
    vkDestroyRenderPass(device, it.render_pass, nullptr);
    for (const VkFramebuffer& fb : ArrayView<VkFramebuffer>{it.framebuffers, static_cast<int>(it.framebuffers_count)})
    {
      vkDestroyFramebuffer(device, fb, nullptr);
    }
  }

  for (const Pipelines::Pair& it : StructureAsArrayView<Pipelines::Pair>(&pipelines))
  {
    vkDestroyPipeline(device, it.pipeline, nullptr);
    vkDestroyPipelineLayout(device, it.layout, nullptr);
  }

  for (VkFence& fence : submition_fences)
  {
    vkDestroyFence(device, fence, nullptr);
  }

  for (auto& image : autoclean_images)
  {
    vkDestroyImage(device, image, nullptr);
  }

  for (auto& image_view : autoclean_image_views)
  {
    vkDestroyImageView(device, image_view, nullptr);
  }

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    vkDestroyImageView(device, msaa_color_image.image_view, nullptr);
    vkDestroyImage(device, msaa_color_image.image, nullptr);
  }

  vkDestroyImageView(device, depth_image.image_view, nullptr);
  vkDestroyImage(device, depth_image.image, nullptr);

  for (const GpuMemoryBlock& it : StructureAsArrayView<GpuMemoryBlock>(&memory_blocks))
  {
    vkFreeMemory(device, it.memory, nullptr);
  }

  vkDestroyBuffer(device, gpu_device_local_memory_buffer, nullptr);
  vkDestroyBuffer(device, gpu_host_visible_transfer_source_memory_buffer, nullptr);
  vkDestroyBuffer(device, gpu_host_coherent_memory_buffer, nullptr);
  vkDestroyBuffer(device, gpu_host_coherent_ubo_memory_buffer, nullptr);

  vkDestroySampler(device, shadowmap_sampler, nullptr);
  vkDestroySampler(device, texture_sampler, nullptr);

  vkDestroySemaphore(device, image_available, nullptr);
  vkDestroySemaphore(device, render_finished, nullptr);

  vkDestroyCommandPool(device, graphics_command_pool, nullptr);
  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

  for (const VkImageView& swapchain_image_view : swapchain_image_views)
  {
    vkDestroyImageView(device, swapchain_image_view, nullptr);
  }

  vkDestroySwapchainKHR(device, swapchain, nullptr);

  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  SDL_DestroyWindow(window);

  if (VK_NULL_HANDLE != debug_callback)
  {
    using Fcn = PFN_vkDestroyDebugUtilsMessengerEXT;
    auto fcn  = (Fcn)(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    SDL_assert(fcn);
    fcn(instance, debug_callback, nullptr);
  }

  vkDestroyInstance(instance, nullptr);

  //generic_allocator.teardown();
}

Texture Engine::load_texture(const char* filepath, bool register_for_destruction)
{
  int             x           = 0;
  int             y           = 0;
  int             real_format = 0;
  SDL_PixelFormat format      = {.format = SDL_PIXELFORMAT_RGBA32, .BitsPerPixel = 32, .BytesPerPixel = (32 + 7) / 8};
  stbi_uc*        pixels      = stbi_load(filepath, &x, &y, &real_format, STBI_rgb_alpha);

  SDL_assert(nullptr != pixels);

  SDL_Surface image_surface = {.format = &format, .w = x, .h = y, .pitch = 4 * x, .pixels = pixels};
  Texture     result        = load_texture(&image_surface, register_for_destruction);
  stbi_image_free(pixels);

  return result;
}

namespace {

VkFormat bitsPerPixelToFormat(uint8_t bpp)
{
  switch (bpp)
  {
  default:
  case 32:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case 8:
    return VK_FORMAT_R8_UNORM;
  }
}

VkFormat bitsPerPixelToFormat(SDL_Surface* surface)
{
  return bitsPerPixelToFormat(surface->format->BitsPerPixel);
}

} // namespace

#if 0
Texture Engine::load_texture_hdr(const char* filename)
{
  int x           = 0;
  int y           = 0;
  int real_format = 0;

  float*         pixels     = stbi_loadf(filename, &x, &y, &real_format, 0);
  const VkFormat dst_format = VK_FORMAT_R32G32B32A32_SFLOAT;

  VkImage        staging_image  = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = dst_format,
        .extent        = {.width = static_cast<uint32_t>(x), .height = static_cast<uint32_t>(y), .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_LINEAR,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(device, &ci, nullptr, &staging_image);
  }

  VkMemoryRequirements reqs = {};
  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    vkGetImageMemoryRequirements(device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, type),
    };

    vkAllocateMemory(device, &allocate, nullptr, &staging_memory);
    vkBindImageMemory(device, staging_image, staging_memory, 0);
  }

  VkSubresourceLayout subresource_layout{};
  VkImageSubresource  image_subresource{};

  image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vkGetImageSubresourceLayout(device, staging_image, &image_subresource, &subresource_layout);

  float* mapped_data = nullptr;
  vkMapMemory(device, staging_memory, 0, reqs.size, 0, (void**)&mapped_data);

  for (int i = 0; i < (x * y); ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      mapped_data[i * 4 + j] = pixels[i * 3 + j];
    }
    mapped_data[i * 4 + 3] = 0.0f;
  }

  vkUnmapMemory(device, staging_memory);

  Texture result = {};

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = dst_format,
        .extent        = {.width = static_cast<uint32_t>(x), .height = static_cast<uint32_t>(y), .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(device, &ci, nullptr, &result.image);
  }

  VkMemoryRequirements result_image_reqs = {};
  vkGetImageMemoryRequirements(device, result.image, &result_image_reqs);

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, result.image, &reqs);
    vkBindImageMemory(device, result.image, memory_blocks.device_images.memory, memory_blocks.device_images.stack_pointer);
    memory_blocks.device_images.stack_pointer += align(reqs.size, memory_blocks.device_images.alignment);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = result.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = dst_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &result.image_view);
  }

  autoclean_images.push(result.image);
  autoclean_image_views.push(result.image_view);

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(device, &allocate, &command_buffer);
  }

  {
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(command_buffer, &begin);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barriers[] = {
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = staging_image,
            .subresourceRange    = sr,
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = result.image,
            .subresourceRange    = sr,
        },
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, SDL_arraysize(barriers), barriers);
  }

  {
    VkImageSubresourceLayers sl = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageCopy copy = {
        .srcSubresource = sl,
        .dstSubresource = sl,
        .extent         = {.width = static_cast<uint32_t>(x), .height = static_cast<uint32_t>(y), .depth = 1},
    };

    vkCmdCopyImage(command_buffer, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = result.image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence image_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &command_buffer,
    };

    vkQueueSubmit(graphics_queue, 1, &submit, image_upload_fence);
  }

  vkWaitForFences(device, 1, &image_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(device, image_upload_fence, nullptr);
  vkFreeMemory(device, staging_memory, nullptr);
  vkDestroyImage(device, staging_image, nullptr);

  stbi_image_free(pixels);

  return result;
}
#endif

Texture Engine::load_texture(SDL_Surface* surface, bool register_for_destruction)
{
  VkImage        staging_image  = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = bitsPerPixelToFormat(surface),
        .extent = {.width = static_cast<uint32_t>(surface->w), .height = static_cast<uint32_t>(surface->h), .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_LINEAR,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(device, &ci, nullptr, &staging_image);
  }

  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, type),
    };

    vkAllocateMemory(device, &allocate, nullptr, &staging_memory);
    vkBindImageMemory(device, staging_image, staging_memory, 0);
  }

  VkSubresourceLayout subresource_layout{};
  VkImageSubresource  image_subresource{};

  image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vkGetImageSubresourceLayout(device, staging_image, &image_subresource, &subresource_layout);

  uint32_t device_row_pitch = static_cast<uint32_t>(subresource_layout.rowPitch);
  uint32_t device_size      = static_cast<uint32_t>(subresource_layout.size);
  uint32_t bytes_per_pixel  = surface->format->BytesPerPixel;
  uint32_t texture_width    = static_cast<uint32_t>(surface->w);
  uint32_t texture_height   = static_cast<uint32_t>(surface->h);
  uint32_t image_size       = texture_width * texture_height * bytes_per_pixel;
  int      image_pitch      = surface->pitch;
  uint32_t pitch_count      = image_size / image_pitch;
  uint8_t* pixels           = reinterpret_cast<uint8_t*>(surface->pixels);
  void*    mapped_data      = nullptr;

  vkMapMemory(device, staging_memory, 0, device_size, 0, &mapped_data);

  uint8_t* pixel_ptr  = pixels;
  uint8_t* mapped_ptr = reinterpret_cast<uint8_t*>(mapped_data);

  if (3 == bytes_per_pixel)
  {
    // we'll set alpha channel to 0xFF for all pixels since most gpus don't support VK_FORMAT_R8G8B8_UNORM
    // @todo: optimize this thing! It's so ugly I want to rip my eyes only glancing at this :(
    int dst_pixel_cnt = 0;
    int trio_counter  = 0;

    for (uint32_t i = 0; i < image_pitch * pitch_count; ++i)
    {
      mapped_ptr[dst_pixel_cnt] = pixel_ptr[i];
      dst_pixel_cnt++;
      trio_counter++;

      if (3 == trio_counter)
      {
        mapped_ptr[dst_pixel_cnt] = 0xFF;
        dst_pixel_cnt++;
        trio_counter = 0;
      }
    }
  }
  else
  {
    for (uint32_t j = 0; j < pitch_count; ++j)
    {
      SDL_memcpy(mapped_ptr, pixel_ptr, static_cast<size_t>(image_pitch));
      mapped_ptr += device_row_pitch;
      pixel_ptr += image_pitch;
    }
  }

  vkUnmapMemory(device, staging_memory);

  Texture result = {};

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = bitsPerPixelToFormat(surface),
        .extent        = {.width = texture_width, .height = texture_height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(device, &ci, nullptr, &result.image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, result.image, &reqs);
    result.memory_offset = memory_blocks.device_images.allocator.allocate_bytes(align(reqs.size, reqs.alignment));
    vkBindImageMemory(device, result.image, memory_blocks.device_images.memory, result.memory_offset);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = result.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = bitsPerPixelToFormat(surface),
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &result.image_view);
  }

  if (register_for_destruction)
  {
    autoclean_images.push(result.image);
    autoclean_image_views.push(result.image_view);
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(device, &allocate, &command_buffer);
  }

  {
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(command_buffer, &begin);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barriers[] = {
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = staging_image,
            .subresourceRange    = sr,
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = result.image,
            .subresourceRange    = sr,
        },
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, SDL_arraysize(barriers), barriers);
  }

  {
    VkImageSubresourceLayers sl = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageCopy copy = {
        .srcSubresource = sl,
        .dstSubresource = sl,
        .extent         = {.width = texture_width, .height = texture_height, .depth = 1},
    };

    vkCmdCopyImage(command_buffer, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = result.image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence image_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &command_buffer,
    };
    vkQueueSubmit(graphics_queue, 1, &submit, image_upload_fence);
  }

  vkWaitForFences(device, 1, &image_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(device, image_upload_fence, nullptr);
  vkFreeMemory(device, staging_memory, nullptr);
  vkDestroyImage(device, staging_image, nullptr);

  return result;
}

namespace {

struct TrianglesVertex
{
  float position[3];
  float normal[3];
  float tex_coord[2];
};

struct ImguiVertex
{
  float    position[2];
  float    tex_coord[2];
  uint32_t color;
};

} // namespace

VkShaderModule Engine::load_shader(const char* file_path) const
{
  //
  // offline compilation process:
  // assets/shader_name.frag --sha256--> bin/obfuscated_shader_name (last 5 bytes / 10 characters)
  //
  // Real name of shader will be stored in binary .text data segment.
  // Obfuscated file name has to be calculated here at runtime.
  //

  SHA256_CTX ctx = {};
  sha256_init(&ctx);
  sha256_update(&ctx, reinterpret_cast<const uint8_t*>(file_path), SDL_strlen(file_path));

  uint8_t hash[32] = {};
  sha256_final(&ctx, hash);

  char hash_string[65] = {};
  for (int i = 0; i < 32; ++i)
  {
    uint8_t first  = static_cast<uint8_t>((hash[i] & 0xF0) >> 4u);
    uint8_t second = static_cast<uint8_t>(hash[i] & 0x0F);

    auto to_char = [](uint8_t in) -> char {
      if (in < 0x0A)
        return in + '0';
      return (in - uint8_t(0x0A)) + 'a';
    };

    hash_string[2 * i + 0] = to_char(first);
    hash_string[2 * i + 1] = to_char(second);
  }
  hash_string[64] = '\0';

  SDL_RWops* handle = SDL_RWFromFile(&hash_string[54], "rb");
  SDL_assert(handle);
  uint32_t file_length = static_cast<uint32_t>(SDL_RWsize(handle));
  uint8_t* buffer      = static_cast<uint8_t*>(SDL_malloc(file_length));

  SDL_RWread(handle, buffer, sizeof(uint8_t), file_length);
  SDL_RWclose(handle);

  VkShaderModuleCreateInfo ci = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext    = nullptr,
      .flags    = 0,
      .codeSize = file_length,
      .pCode    = reinterpret_cast<uint32_t*>(buffer),
  };

  VkShaderModule result = VK_NULL_HANDLE;
  vkCreateShaderModule(device, &ci, nullptr, &result);
  SDL_free(buffer);

  return result;
}

void RenderPass::begin(VkCommandBuffer cmd, uint32_t image_index) const
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

void Engine::change_resolution(const VkExtent2D new_size)
{
  extent2D = new_size;

  vkDeviceWaitIdle(device);
  SDL_SetWindowSize(window, extent2D.width, extent2D.height);

  for (const RenderPass& it : StructureAsArrayView<RenderPass>(&render_passes))
  {
    vkDestroyRenderPass(device, it.render_pass, nullptr);
    for (uint32_t i = 0; i < it.framebuffers_count; ++i)
      vkDestroyFramebuffer(device, it.framebuffers[i], nullptr);
  }

  for (const Pipelines::Pair& it : StructureAsArrayView<Pipelines::Pair>(&pipelines))
  {
    vkDestroyPipeline(device, it.pipeline, nullptr);
    vkDestroyPipelineLayout(device, it.layout, nullptr);
  }

  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);

  // re-creating new swapchain
  {
    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = SWAPCHAIN_IMAGES_COUNT,
        .imageFormat      = surface_format.format,
        .imageColorSpace  = surface_format.colorSpace,
        .imageExtent      = extent2D,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = surface_capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = present_mode,
        .clipped          = VK_TRUE,
    };

    vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);

    uint32_t swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, nullptr);
    SDL_assert(SWAPCHAIN_IMAGES_COUNT == swapchain_images_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, swapchain_images);
  }

  // new image views for swapchain
  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      VkImageViewCreateInfo ci = {
          .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image            = swapchain_images[i],
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = surface_format.format,
          .subresourceRange = sr,
      };

      vkDestroyImageView(device, swapchain_image_views[i], nullptr);
      vkCreateImageView(device, &ci, nullptr, &swapchain_image_views[i]);
    }
  }

  // re-creating render pass images
  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, msaa_color_image.image, &reqs);
    memory_blocks.device_images.allocator.free_bytes(msaa_color_image.memory_offset, align(reqs.size, reqs.alignment));
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, depth_image.image, &reqs);
    memory_blocks.device_images.allocator.free_bytes(depth_image.memory_offset, align(reqs.size, reqs.alignment));
  }

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = surface_format.format,
        .extent        = {.width = extent2D.width, .height = extent2D.height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = MSAA_SAMPLE_COUNT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkDestroyImage(device, msaa_color_image.image, nullptr);
    vkCreateImage(device, &ci, nullptr, &msaa_color_image.image);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, msaa_color_image.image, &reqs);
    msaa_color_image.memory_offset =
        memory_blocks.device_images.allocator.allocate_bytes(align(reqs.size, reqs.alignment));
    vkBindImageMemory(device, msaa_color_image.image, memory_blocks.device_images.memory,
                      msaa_color_image.memory_offset);
  }

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {.width = extent2D.width, .height = extent2D.height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = MSAA_SAMPLE_COUNT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkDestroyImage(device, depth_image.image, nullptr);
    vkCreateImage(device, &ci, nullptr, &depth_image.image);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, depth_image.image, &reqs);
    depth_image.memory_offset = memory_blocks.device_images.allocator.allocate_bytes(align(reqs.size, reqs.alignment));
    vkBindImageMemory(device, depth_image.image, memory_blocks.device_images.memory, depth_image.memory_offset);
  }

  // image views can only be created when memory is bound to the image handle

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = msaa_color_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = surface_format.format,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkDestroyImageView(device, msaa_color_image.image_view, nullptr);
    vkCreateImageView(device, &ci, nullptr, &msaa_color_image.image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = depth_image.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = gen_rgba_cm(),
        .subresourceRange = sr,
    };

    vkDestroyImageView(device, depth_image.image_view, nullptr);
    vkCreateImageView(device, &ci, nullptr, &depth_image.image_view);
  }

  // transitioning depth image into correct layout
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo info = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };
      vkAllocateCommandBuffers(device, &info, &cmd);
    }

    {
      VkCommandBufferBeginInfo info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };
      vkBeginCommandBuffer(cmd, &info);
    }

    {
      VkImageMemoryBarrier barrier = {
          // depth test attachment
          .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image               = depth_image.image,
          .subresourceRange =
              {
                  .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                  .baseMipLevel   = 0,
                  .levelCount     = 1,
                  .baseArrayLayer = 0,
                  .layerCount     = 1,
              },
      };

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &barrier);

      vkEndCommandBuffer(cmd);

      {
        VkSubmitInfo info = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &cmd,
        };
        vkQueueSubmit(graphics_queue, 1, &info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);
      }

      vkFreeCommandBuffers(device, graphics_command_pool, 1, &cmd);
    }
  }

  setup_render_passes();
  setup_framebuffers();
  setup_pipeline_layouts();
  setup_pipelines();
}

void Engine::insert_debug_marker(VkCommandBuffer cmd, const char* name, const Vec4& color) const
{
  if (not renderdoc_marker_naming_enabled)
    return;

  VkDebugMarkerMarkerInfoEXT marker_info = {
      .sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
      .pMarkerName = name,
      .color       = {color.x, color.y, color.z, color.w},
  };

  vkCmdDebugMarkerInsert(cmd, &marker_info);
}
