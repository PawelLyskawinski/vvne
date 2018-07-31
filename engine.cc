#ifdef __linux__
#define VK_USE_PLATFORM_XCB_KHR
#else
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "engine.hh"
#include "pipelines.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_vulkan.h>
#include <linmath.h>

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <SDL2/SDL_timer.h>
#include <stb_image.h>
#pragma GCC diagnostic pop

#define INITIAL_WINDOW_WIDTH 1200
#define INITIAL_WINDOW_HEIGHT 800

VkBool32
#ifndef __linux__
    __stdcall
#endif
    vulkan_debug_callback(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT, uint64_t, size_t, int32_t, const char*,
                          const char* msg, void*)
{
  SDL_Log("validation layer: %s", msg);
  return VK_FALSE;
}

namespace {

uint32_t find_memory_type_index(VkPhysicalDeviceMemoryProperties* properties, VkMemoryRequirements* reqs,
                                VkMemoryPropertyFlags searched)
{
  for (uint32_t i = 0; i < properties->memoryTypeCount; ++i)
  {
    if (0 == (reqs->memoryTypeBits & (1 << i)))
      continue;

    VkMemoryPropertyFlags memory_type_properties = properties->memoryTypes[i].propertyFlags;

    if (searched == (memory_type_properties & searched))
    {
      return i;
    }
  }

  // this code fragment should never be reached!
  SDL_assert(false);
  return 0;
}

} // namespace

void Engine::startup()
{
  GenericHandles& ctx = generic_handles;

  {
    VkApplicationInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "cvk",
        .applicationVersion = 1,
        .pEngineName        = "cvk_engine",
        .engineVersion      = 1,
        .apiVersion         = VK_API_VERSION_1_0,
    };

#ifdef ENABLE_VK_VALIDATION
    const char* instance_layers[] = {"VK_LAYER_LUNARG_standard_validation"};
#endif

    const char* instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __linux__
        "VK_KHR_xlib_surface",
#else
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif

#ifdef ENABLE_VK_VALIDATION
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
    };

    VkInstanceCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pApplicationInfo = &ai,
#ifdef ENABLE_VK_VALIDATION
        .enabledLayerCount   = SDL_arraysize(instance_layers),
        .ppEnabledLayerNames = instance_layers,
#endif
        .enabledExtensionCount   = SDL_arraysize(instance_extensions),
        .ppEnabledExtensionNames = instance_extensions,
    };

    vkCreateInstance(&ci, nullptr, &ctx.instance);
  }

#ifdef ENABLE_VK_VALIDATION
  {
    VkDebugReportCallbackCreateInfoEXT ci = {
        .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .pNext       = nullptr,
        .flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = vulkan_debug_callback,
    };

    auto fcn = (PFN_vkCreateDebugReportCallbackEXT)(
        vkGetInstanceProcAddr(generic_handles.instance, "vkCreateDebugReportCallbackEXT"));
    fcn(generic_handles.instance, &ci, nullptr, &generic_handles.debug_callback);
  }
#endif

  ctx.window = SDL_CreateWindow("vvne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, INITIAL_WINDOW_WIDTH,
                                INITIAL_WINDOW_HEIGHT, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

  {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &count, nullptr);
    VkPhysicalDevice* handles = double_ended_stack.allocate_back<VkPhysicalDevice>(count);
    vkEnumeratePhysicalDevices(ctx.instance, &count, handles);

    ctx.physical_device = handles[0];
    vkGetPhysicalDeviceProperties(ctx.physical_device, &ctx.physical_device_properties);
    SDL_Log("Selecting graphics card: %s", ctx.physical_device_properties.deviceName);
  }

  SDL_bool surface_result = SDL_Vulkan_CreateSurface(ctx.window, ctx.instance, &ctx.surface);
  if (SDL_FALSE == surface_result)
  {
    SDL_Log("%s", SDL_GetError());
    return;
  }

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &ctx.surface_capabilities);
  ctx.extent2D = ctx.surface_capabilities.currentExtent;

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &count, nullptr);
    VkQueueFamilyProperties* all_properties = double_ended_stack.allocate_back<VkQueueFamilyProperties>(count);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &count, all_properties);

    for (uint32_t i = 0; count; ++i)
    {
      VkQueueFamilyProperties properties          = all_properties[i];
      VkBool32                has_present_support = 0;
      vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physical_device, i, ctx.surface, &has_present_support);

      if (has_present_support && (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT))
      {
        ctx.graphics_family_index = i;
        break;
      }
    }
  }

  {
#ifdef ENABLE_VK_VALIDATION
    const char* device_layers[] = {"VK_LAYER_LUNARG_standard_validation"};
#endif

    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    float       queue_priorities[]  = {1.0f};

    VkDeviceQueueCreateInfo graphics = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx.graphics_family_index,
        .queueCount       = SDL_arraysize(queue_priorities),
        .pQueuePriorities = queue_priorities,
    };

    VkPhysicalDeviceFeatures device_features = {};

    device_features.sampleRateShading = VK_TRUE;
    device_features.wideLines         = VK_TRUE;

    VkDeviceCreateInfo ci = {
        .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos    = &graphics,
#ifdef ENABLE_VK_VALIDATION
        .enabledLayerCount   = SDL_arraysize(device_layers),
        .ppEnabledLayerNames = device_layers,
#endif
        .enabledExtensionCount   = SDL_arraysize(device_extensions),
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures        = &device_features,
    };

    vkCreateDevice(ctx.physical_device, &ci, nullptr, &ctx.device);
  }

  vkGetDeviceQueue(ctx.device, ctx.graphics_family_index, 0, &ctx.graphics_queue);

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device, ctx.surface, &count, nullptr);
    VkSurfaceFormatKHR* formats = double_ended_stack.allocate_back<VkSurfaceFormatKHR>(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device, ctx.surface, &count, formats);

    ctx.surface_format = formats[0];
    for (uint32_t i = 0; i < count; ++i)
    {
      if (VK_FORMAT_B8G8R8A8_UNORM != formats[i].format)
        continue;

      if (VK_COLOR_SPACE_SRGB_NONLINEAR_KHR != formats[i].colorSpace)
        continue;

      ctx.surface_format = formats[i];
      break;
    }
  }

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device, ctx.surface, &count, nullptr);
    VkPresentModeKHR* present_modes = double_ended_stack.allocate_back<VkPresentModeKHR>(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device, ctx.surface, &count, present_modes);

    ctx.present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; ++i)
    {
      if (VK_PRESENT_MODE_MAILBOX_KHR == present_modes[i])
      {
        ctx.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
    }
  }

  {
    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = ctx.surface,
        .minImageCount    = SWAPCHAIN_IMAGES_COUNT,
        .imageFormat      = ctx.surface_format.format,
        .imageColorSpace  = ctx.surface_format.colorSpace,
        .imageExtent      = ctx.extent2D,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = ctx.surface_capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = ctx.present_mode,
        .clipped          = VK_TRUE,
    };

    vkCreateSwapchainKHR(ctx.device, &ci, nullptr, &ctx.swapchain);

    uint32_t swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &swapchain_images_count, nullptr);
    SDL_assert(SWAPCHAIN_IMAGES_COUNT == swapchain_images_count);
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &swapchain_images_count, ctx.swapchain_images);
  }

  {
    VkComponentMapping cm = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    };

    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      VkImageViewCreateInfo ci = {
          .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image            = ctx.swapchain_images[i],
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = ctx.surface_format.format,
          .components       = cm,
          .subresourceRange = sr,
      };

      vkCreateImageView(ctx.device, &ci, nullptr, &ctx.swapchain_image_views[i]);
    }
  }

  {
    VkCommandPoolCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx.graphics_family_index,
    };

    vkCreateCommandPool(ctx.device, &ci, nullptr, &ctx.graphics_command_pool);
  }

  // Pool sizes below are just an suggestions. They have to be adjusted for the final release builds
  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 * SWAPCHAIN_IMAGES_COUNT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20 * SWAPCHAIN_IMAGES_COUNT},
    };

    VkDescriptorPoolCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 60 * SWAPCHAIN_IMAGES_COUNT,
        .poolSizeCount = SDL_arraysize(pool_sizes),
        .pPoolSizes    = pool_sizes,
    };

    vkCreateDescriptorPool(ctx.device, &ci, nullptr, &ctx.descriptor_pool);
  }

  {
    VkSemaphoreCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(ctx.device, &ci, nullptr, &ctx.image_available);
    vkCreateSemaphore(ctx.device, &ci, nullptr, &ctx.render_finished);
  }

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {.width = ctx.extent2D.width, .height = ctx.extent2D.height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(ctx.device, &ci, nullptr, &ctx.depth_image);
  }

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = ctx.surface_format.format,
        .extent        = {.width = ctx.extent2D.width, .height = ctx.extent2D.height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = MSAA_SAMPLE_COUNT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCreateImage(ctx.device, &ci, nullptr, &ctx.msaa_color_image);
  }

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {.width = ctx.extent2D.width, .height = ctx.extent2D.height, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = MSAA_SAMPLE_COUNT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCreateImage(ctx.device, &ci, nullptr, &ctx.msaa_depth_image);
  }

  {
    VkSamplerCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_FALSE,
        .maxAnisotropy           = 1,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    vkCreateSampler(ctx.device, &ci, nullptr, &ctx.texture_sampler);
  }

  // STATIC_GEOMETRY

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GpuStaticGeometry::MAX_MEMORY_SIZE,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ci.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    vkCreateBuffer(ctx.device, &ci, nullptr, &gpu_static_geometry.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, gpu_static_geometry.buffer, &reqs);
    gpu_static_geometry.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &gpu_static_geometry.memory);
    vkBindBufferMemory(ctx.device, gpu_static_geometry.buffer, gpu_static_geometry.memory, 0);
  }

  // STATIC_GEOMETRY_TRANSFER
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GpuStaticTransfer::MAX_MEMORY_SIZE,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(ctx.device, &ci, nullptr, &gpu_static_transfer.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, gpu_static_transfer.buffer, &reqs);
    gpu_static_transfer.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(
            &properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &gpu_static_transfer.memory);
    vkBindBufferMemory(ctx.device, gpu_static_transfer.buffer, gpu_static_transfer.memory, 0);
  }

  // HOST VISIBLE

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GpuHostVisible::MAX_MEMORY_SIZE,
        .usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(ctx.device, &ci, nullptr, &gpu_host_visible.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, gpu_host_visible.buffer, &reqs);
    gpu_host_visible.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &gpu_host_visible.memory);
    vkBindBufferMemory(ctx.device, gpu_host_visible.buffer, gpu_host_visible.memory, 0);
  }

  // IMAGES
  {
    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(ctx.device, ctx.depth_image, &reqs);
    images.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = Images::MAX_MEMORY_SIZE,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &images.memory);
    vkBindImageMemory(ctx.device, ctx.depth_image, images.memory, images.allocate(reqs.size));
  }

  {
    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(ctx.device, ctx.msaa_color_image, &reqs);
    vkBindImageMemory(ctx.device, ctx.msaa_color_image, images.memory, images.allocate(reqs.size));
  }

  {
    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(ctx.device, ctx.msaa_depth_image, &reqs);
    vkBindImageMemory(ctx.device, ctx.msaa_depth_image, images.memory, images.allocate(reqs.size));
  }

  {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo alloc = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = ctx.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(ctx.device, &alloc, &command_buffer);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(command_buffer, &begin);
    }

    {
      VkImageMemoryBarrier barrier = {
          .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image               = ctx.depth_image,
          .subresourceRange    = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1},
      };

      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(command_buffer);

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &command_buffer,
      };

      vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    }

    vkQueueWaitIdle(ctx.graphics_queue);
    vkFreeCommandBuffers(ctx.device, ctx.graphics_command_pool, 1, &command_buffer);
  }

  // image views can only be created when memory is bound to the image handle
  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkComponentMapping comp = {
        .r = VK_COMPONENT_SWIZZLE_R,
        .g = VK_COMPONENT_SWIZZLE_G,
        .b = VK_COMPONENT_SWIZZLE_B,
        .a = VK_COMPONENT_SWIZZLE_A,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx.depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(ctx.device, &ci, nullptr, &ctx.depth_image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkComponentMapping comp = {
        .r = VK_COMPONENT_SWIZZLE_R,
        .g = VK_COMPONENT_SWIZZLE_G,
        .b = VK_COMPONENT_SWIZZLE_B,
        .a = VK_COMPONENT_SWIZZLE_A,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx.msaa_color_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = ctx.surface_format.format,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(ctx.device, &ci, nullptr, &ctx.msaa_color_image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkComponentMapping comp = {
        .r = VK_COMPONENT_SWIZZLE_R,
        .g = VK_COMPONENT_SWIZZLE_G,
        .b = VK_COMPONENT_SWIZZLE_B,
        .a = VK_COMPONENT_SWIZZLE_A,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx.msaa_depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(ctx.device, &ci, nullptr, &ctx.msaa_depth_image_view);
  }

  images.images      = double_ended_stack.allocate_front<VkImage>(Images::MAX_COUNT);
  images.image_views = double_ended_stack.allocate_front<VkImageView>(Images::MAX_COUNT);
  images.add(ctx.depth_image, ctx.depth_image_view);
  images.add(ctx.msaa_color_image, ctx.msaa_color_image_view);
  images.add(ctx.msaa_depth_image, ctx.msaa_depth_image_view);

  // UBO HOST VISIBLE
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = UboHostVisible::MAX_MEMORY_SIZE,
        .usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(ctx.device, &ci, nullptr, &ubo_host_visible.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, ubo_host_visible.buffer, &reqs);
    ubo_host_visible.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(
            &properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &ubo_host_visible.memory);
    vkBindBufferMemory(ctx.device, ubo_host_visible.buffer, ubo_host_visible.memory, 0);
  }

  double_ended_stack.reset_back();
  setup_simple_rendering();
}

void Engine::teardown()
{
  GenericHandles& ctx = generic_handles;

  vkDeviceWaitIdle(ctx.device);

  for (int i = 0; i < scheduled_pipelines_destruction_count; ++i)
  {
    vkDestroyPipeline(generic_handles.device, scheduled_pipelines_destruction[i].pipeline, nullptr);
  }

  vkDestroyRenderPass(ctx.device, simple_rendering.render_pass, nullptr);

  {
    VkDescriptorSetLayout layouts[] = {
        simple_rendering.pbr_metallic_workflow_material_descriptor_set_layout,
        simple_rendering.pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout,
        simple_rendering.pbr_dynamic_lights_descriptor_set_layout,
        simple_rendering.single_texture_in_frag_descriptor_set_layout,
        simple_rendering.skinning_matrices_descriptor_set_layout,
    };

    for (VkDescriptorSetLayout layout : layouts)
      vkDestroyDescriptorSetLayout(ctx.device, layout, nullptr);
  }

  for (VkFramebuffer& framebuffer : simple_rendering.framebuffers)
    vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);

  for (VkPipelineLayout& layout : simple_rendering.pipeline_layouts)
    vkDestroyPipelineLayout(ctx.device, layout, nullptr);

  for (VkPipeline& pipeline : simple_rendering.pipelines)
    vkDestroyPipeline(ctx.device, pipeline, nullptr);

  for (VkFence& fence : simple_rendering.submition_fences)
    vkDestroyFence(ctx.device, fence, nullptr);

  for (uint32_t i = 0; i < images.loaded_count; ++i)
  {
    vkDestroyImage(ctx.device, images.images[i], nullptr);
  }

  for (uint32_t i = 0; i < images.loaded_count; ++i)
  {
    vkDestroyImageView(ctx.device, images.image_views[i], nullptr);
  }

  {
    VkDeviceMemory memories[] = {ubo_host_visible.memory, images.memory, gpu_host_visible.memory,
                                 gpu_static_transfer.memory, gpu_static_geometry.memory};

    for (VkDeviceMemory memory : memories)
      vkFreeMemory(ctx.device, memory, nullptr);
  }

  {
    VkBuffer buffers[] = {ubo_host_visible.buffer, gpu_host_visible.buffer, gpu_static_transfer.buffer,
                          gpu_static_geometry.buffer};

    for (VkBuffer buffer : buffers)
      vkDestroyBuffer(ctx.device, buffer, nullptr);
  }

  vkDestroySampler(ctx.device, ctx.texture_sampler, nullptr);
  vkDestroySemaphore(ctx.device, ctx.image_available, nullptr);
  vkDestroySemaphore(ctx.device, ctx.render_finished, nullptr);
  vkDestroyCommandPool(ctx.device, ctx.graphics_command_pool, nullptr);
  vkDestroyDescriptorPool(ctx.device, ctx.descriptor_pool, nullptr);

  for (VkImageView swapchain_image_view : ctx.swapchain_image_views)
  {
    vkDestroyImageView(ctx.device, swapchain_image_view, nullptr);
  }

  vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
  vkDestroyDevice(ctx.device, nullptr);
  vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
  SDL_DestroyWindow(ctx.window);

#ifdef ENABLE_VK_VALIDATION
  using Fcn = PFN_vkDestroyDebugReportCallbackEXT;
  auto fcn  = (Fcn)(vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugReportCallbackEXT"));
  fcn(ctx.instance, ctx.debug_callback, nullptr);
#endif

  vkDestroyInstance(ctx.instance, nullptr);
}

int Engine::load_texture(const char* filepath)
{
  int x           = 0;
  int y           = 0;
  int real_format = 0;
  int result      = 0;

  stbi_uc*     pixels       = stbi_load(filepath, &x, &y, &real_format, STBI_rgb_alpha);
  int          depth        = 32;
  int          pitch        = 4 * x;
  Uint32       pixel_format = SDL_PIXELFORMAT_RGBA32;
  SDL_Surface* surface      = SDL_CreateRGBSurfaceWithFormatFrom(pixels, x, y, depth, pitch, pixel_format);

  result = load_texture(surface);

  SDL_FreeSurface(surface);
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

int Engine::load_texture_hdr(const char* filename)
{
  int x           = 0;
  int y           = 0;
  int real_format = 0;

  float*         pixels     = stbi_loadf(filename, &x, &y, &real_format, 0);
  const VkFormat dst_format = VK_FORMAT_R32G32B32A32_SFLOAT;

  GenericHandles& ctx            = generic_handles;
  VkImage         staging_image  = VK_NULL_HANDLE;
  VkDeviceMemory  staging_memory = VK_NULL_HANDLE;

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

    vkCreateImage(ctx.device, &ci, nullptr, &staging_image);
  }

  VkMemoryRequirements reqs = {};
  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);
    vkGetImageMemoryRequirements(ctx.device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, type),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &staging_memory);
    vkBindImageMemory(ctx.device, staging_image, staging_memory, 0);
  }

  VkSubresourceLayout subresource_layout{};
  VkImageSubresource  image_subresource{};

  image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vkGetImageSubresourceLayout(generic_handles.device, staging_image, &image_subresource, &subresource_layout);

  float* mapped_data = nullptr;
  vkMapMemory(ctx.device, staging_memory, 0, reqs.size, 0, (void**)&mapped_data);

  for (int i = 0; i < (x * y); ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      mapped_data[i * 4 + j] = pixels[i * 3 + j];
    }
    mapped_data[i * 4 + 3] = 0.0f;
  }

  vkUnmapMemory(ctx.device, staging_memory);

  const int resultIdx = images.loaded_count;
  images.loaded_count += 1;
  VkImage&     result_image = images.images[resultIdx];
  VkImageView& result_view  = images.image_views[resultIdx];

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

    vkCreateImage(ctx.device, &ci, nullptr, &result_image);
  }

  VkMemoryRequirements result_image_reqs = {};
  vkGetImageMemoryRequirements(ctx.device, result_image, &result_image_reqs);

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(ctx.device, result_image, &reqs);
    vkBindImageMemory(ctx.device, result_image, images.memory, images.allocate(reqs.size));
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
        .image            = result_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = dst_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(ctx.device, &ci, nullptr, &result_view);
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(ctx.device, &allocate, &command_buffer);
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
            .image               = result_image,
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

    vkCmdCopyImage(command_buffer, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result_image,
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
        .image               = result_image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence image_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(ctx.device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &command_buffer,
    };

    vkQueueSubmit(ctx.graphics_queue, 1, &submit, image_upload_fence);
  }

  vkWaitForFences(ctx.device, 1, &image_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(ctx.device, image_upload_fence, nullptr);
  vkFreeMemory(ctx.device, staging_memory, nullptr);
  vkDestroyImage(ctx.device, staging_image, nullptr);

  stbi_image_free(pixels);

  return resultIdx;
}

int Engine::load_texture(SDL_Surface* surface)
{
  GenericHandles& ctx            = generic_handles;
  VkImage         staging_image  = VK_NULL_HANDLE;
  VkDeviceMemory  staging_memory = VK_NULL_HANDLE;

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

    vkCreateImage(ctx.device, &ci, nullptr, &staging_image);
  }

  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(ctx.device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, type),
    };

    vkAllocateMemory(ctx.device, &allocate, nullptr, &staging_memory);
    vkBindImageMemory(ctx.device, staging_image, staging_memory, 0);
  }

  VkSubresourceLayout subresource_layout{};
  VkImageSubresource  image_subresource{};

  image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vkGetImageSubresourceLayout(generic_handles.device, staging_image, &image_subresource, &subresource_layout);

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

  vkMapMemory(ctx.device, staging_memory, 0, device_size, 0, &mapped_data);

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

  vkUnmapMemory(ctx.device, staging_memory);

  const int resultIdx = images.loaded_count;
  images.loaded_count += 1;
  VkImage&     result_image = images.images[resultIdx];
  VkImageView& result_view  = images.image_views[resultIdx];

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

    vkCreateImage(ctx.device, &ci, nullptr, &result_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(ctx.device, result_image, &reqs);
    vkBindImageMemory(ctx.device, result_image, images.memory, images.allocate(reqs.size));
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
        .image            = result_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = bitsPerPixelToFormat(surface),
        .subresourceRange = sr,
    };

    vkCreateImageView(ctx.device, &ci, nullptr, &result_view);
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(ctx.device, &allocate, &command_buffer);
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
            .image               = result_image,
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

    vkCmdCopyImage(command_buffer, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result_image,
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
        .image               = result_image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence image_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(ctx.device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &command_buffer,
    };
    vkQueueSubmit(ctx.graphics_queue, 1, &submit, image_upload_fence);
  }

  vkWaitForFences(ctx.device, 1, &image_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(ctx.device, image_upload_fence, nullptr);
  vkFreeMemory(ctx.device, staging_memory, nullptr);
  vkDestroyImage(ctx.device, staging_image, nullptr);

  return resultIdx;
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

VkShaderModule Engine::load_shader(const char* file_path)
{
  SDL_RWops* handle      = SDL_RWFromFile(file_path, "rb");
  uint32_t   file_length = static_cast<uint32_t>(SDL_RWsize(handle));
  uint8_t*   buffer      = static_cast<uint8_t*>(SDL_malloc(file_length));

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
  vkCreateShaderModule(generic_handles.device, &ci, nullptr, &result);
  SDL_free(buffer);

  return result;
}

void Engine::setup_simple_rendering()
{
  GenericHandles&  ctx      = generic_handles;
  SimpleRendering& renderer = simple_rendering;

  {
    VkAttachmentDescription attachments[] = {
        {
            .format         = ctx.surface_format.format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        {
            .format         = VK_FORMAT_D32_SFLOAT,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
        {
            .format         = ctx.surface_format.format,
            .samples        = MSAA_SAMPLE_COUNT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

        },
        {

            .format         = VK_FORMAT_D32_SFLOAT,
            .samples        = MSAA_SAMPLE_COUNT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_reference = {
        .attachment = 2,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_reference = {
        .attachment = 3,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference resolve_reference = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpasses[] = {
        {
            // skybox pass
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_reference,
            .pResolveAttachments  = &resolve_reference,
        },
        {
            // objects3d pass
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_reference,
            .pResolveAttachments     = &resolve_reference,
            .pDepthStencilAttachment = &depth_reference,
        },
        {
            // robot gui pass
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_reference,
            .pResolveAttachments  = &resolve_reference,
        },
        {
            // robot gui radar dots pass
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_reference,
            .pResolveAttachments  = &resolve_reference,
        },
        {
            // imgui pass
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_reference,
            .pResolveAttachments  = &resolve_reference,
        },
    };

    VkSubpassDependency dependencies[] = {
        {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = SimpleRendering::Pass::Skybox,
            .srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass    = SimpleRendering::Pass::Skybox,
            .dstSubpass    = SimpleRendering::Pass::Objects3D,
            .srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass    = SimpleRendering::Pass::Objects3D,
            .dstSubpass    = SimpleRendering::Pass::RobotGui,
            .srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass    = SimpleRendering::Pass::RobotGui,
            .dstSubpass    = SimpleRendering::Pass::RadarDots,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass    = SimpleRendering::Pass::RadarDots,
            .dstSubpass    = SimpleRendering::Pass::ImGui,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
    };

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = SDL_arraysize(attachments),
        .pAttachments    = attachments,
        .subpassCount    = SDL_arraysize(subpasses),
        .pSubpasses      = subpasses,
        .dependencyCount = SDL_arraysize(dependencies),
        .pDependencies   = dependencies,
    };

    vkCreateRenderPass(ctx.device, &ci, nullptr, &renderer.render_pass);
  }

  // --------------------------------------------------------------- //
  // Metallic workflow PBR materials descriptor set layout
  //
  // texture ordering:
  // 0. albedo
  // 1. metallic roughness (r: UNUSED, b: metallness, g: roughness)
  // 2. emissive
  // 3. ambient occlusion
  // 4. normal
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 5,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr,
                                &renderer.pbr_metallic_workflow_material_descriptor_set_layout);
  }

  // --------------------------------------------------------------- //
  // PBR IBL cubemaps and BRDF lookup table
  //
  // texture ordering:
  // 0.0 irradiance (cubemap)
  // 0.1 prefiltered (cubemap)
  // 1   BRDF lookup table (2D)
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 2,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SDL_arraysize(bindings),
        .pBindings    = bindings,
    };

    vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr,
                                &renderer.pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout);
  }

  // --------------------------------------------------------------- //
  // PBR dynamic light sources
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr, &renderer.pbr_dynamic_lights_descriptor_set_layout);
  }

  // --------------------------------------------------------------- //
  // Single texture in fragment shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr, &renderer.single_texture_in_frag_descriptor_set_layout);
  }

  // --------------------------------------------------------------- //
  // Skinning matrices in vertex shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr, &renderer.skinning_matrices_descriptor_set_layout);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 2 * sizeof(mat4x4),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Pipeline::Skybox]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 3 * sizeof(mat4x4) + sizeof(vec3),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.pbr_metallic_workflow_material_descriptor_set_layout,
                                               renderer.pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout,
                                               renderer.pbr_dynamic_lights_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Pipeline::Scene3D]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {

            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::ColoredGeometry]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {
        renderer.skinning_matrices_descriptor_set_layout,
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::ColoredGeometrySkinned]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGui]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = sizeof(vec4),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiLines]);
  }

  {
    struct VertexPushConstant
    {
      mat4x4 mvp;
      vec2   character_coordinate;
      vec2   character_size;
    };

    struct FragmentPushConstant
    {
      vec3  color;
      float time;
    };

    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(VertexPushConstant),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(VertexPushConstant),
            .size       = sizeof(FragmentPushConstant),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiSdfFont]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 2 * sizeof(vec4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 2 * sizeof(vec4),
            .size       = sizeof(vec4),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiTriangle]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(vec4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(vec4),
            .size       = sizeof(vec4),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr,
                           &renderer.pipeline_layouts[SimpleRendering::Pipeline::GreenGuiRadarDots]);
  }

  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 16 * sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {renderer.single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Pipeline::ImGui]);
  }

  pipeline_reload_simple_rendering_skybox_reload(*this);
  pipeline_reload_simple_rendering_scene3d_reload(*this);
  pipeline_reload_simple_rendering_coloredgeometry_reload(*this);
  pipeline_reload_simple_rendering_coloredgeometryskinned_reload(*this);
  pipeline_reload_simple_rendering_green_gui_reload(*this);
  pipeline_reload_simple_rendering_green_gui_weapon_selector_box_left_reload(*this);
  pipeline_reload_simple_rendering_green_gui_weapon_selector_box_right_reload(*this);
  pipeline_reload_simple_rendering_green_gui_lines_reload(*this);
  pipeline_reload_simple_rendering_green_gui_sdf_reload(*this);
  pipeline_reload_simple_rendering_green_gui_triangle_reload(*this);
  pipeline_reload_simple_rendering_green_gui_radar_dots_reload(*this);
  pipeline_reload_simple_rendering_imgui_reload(*this);

  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments[] = {ctx.swapchain_image_views[i], ctx.depth_image_view, ctx.msaa_color_image_view,
                                 ctx.msaa_depth_image_view};

    VkFramebufferCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = renderer.render_pass,
        .attachmentCount = SDL_arraysize(attachments),
        .pAttachments    = attachments,
        .width           = ctx.extent2D.width,
        .height          = ctx.extent2D.height,
        .layers          = 1,
    };

    vkCreateFramebuffer(ctx.device, &ci, nullptr, &renderer.framebuffers[i]);
  }

  for (auto& submition_fence : renderer.submition_fences)

  {
    VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    vkCreateFence(ctx.device, &ci, nullptr, &submition_fence);
  }

  {
    VkCommandBufferAllocateInfo alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = SDL_arraysize(renderer.primary_command_buffers),
    };

    vkAllocateCommandBuffers(ctx.device, &alloc, renderer.primary_command_buffers);
  }
}
