#ifdef __linux__
#define VK_USE_PLATFORM_XCB_KHR
#else
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "engine.hh"
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

#define INITIAL_WINDOW_WIDTH 800
#define INITIAL_WINDOW_HEIGHT 600

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
    device_features.sampleRateShading        = VK_TRUE;

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

void Engine::print_memory_statistics()
{
  auto  calc_procent  = [](VkDeviceSize part, VkDeviceSize max) { return 100.0f * ((float)part / (float)max); };
  float image_percent = calc_procent(images.used_memory, Images::MAX_MEMORY_SIZE);
  float dv_percent    = calc_procent(gpu_static_geometry.used_memory, GpuStaticGeometry::MAX_MEMORY_SIZE);
  float hv_percent    = calc_procent(gpu_host_visible.used_memory, GpuHostVisible::MAX_MEMORY_SIZE);
  float ubo_percent   = calc_procent(ubo_host_visible.used_memory, UboHostVisible::MAX_MEMORY_SIZE);
  float stack_percent = 100.0f * (double_ended_stack.front / (float)DoubleEndedStack::MAX_MEMORY_SIZE);

  SDL_Log("### Memory statistics ###");
  SDL_Log("Image memory:                    %.2f proc. out of %u MB", image_percent, Images::MAX_MEMORY_SIZE_MB);
  SDL_Log("device-visible memory:           %.2f proc. out of %u MB", dv_percent,
          GpuStaticGeometry::MAX_MEMORY_SIZE_MB);
  SDL_Log("host-visible memory:             %.2f proc. out of %u MB", hv_percent, GpuHostVisible::MAX_MEMORY_SIZE_MB);
  SDL_Log("universal buffer objects memory: %.2f proc. out of %u MB", ubo_percent, UboHostVisible::MAX_MEMORY_SIZE_MB);
  SDL_Log("double ended stack memory:       %.2f proc. out of %u MB", stack_percent,
          DoubleEndedStack::MAX_MEMORY_SIZE_MB);
}

void Engine::teardown()
{
  GenericHandles& ctx = generic_handles;

  vkDeviceWaitIdle(ctx.device);

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
            .dstSubpass    = SimpleRendering::Pass::ImGui,
            .srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
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

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = load_shader("skybox.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = load_shader("skybox.frag.spv"),
            .pName  = "main",
        },
    };

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
        },
    };

    VkVertexInputBindingDescription vertex_binding_descriptions[] = {
        {
            .binding   = 0,
            .stride    = sizeof(TrianglesVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
        .pVertexBindingDescriptions      = vertex_binding_descriptions,
        .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
        .pVertexAttributeDescriptions    = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewports[] = {
        {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(ctx.extent2D.width),
            .height   = static_cast<float>(ctx.extent2D.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    VkRect2D scissors[] = {
        {
            .offset = {0, 0},
            .extent = ctx.extent2D,
        },
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = SDL_arraysize(viewports),
        .pViewports    = viewports,
        .scissorCount  = SDL_arraysize(scissors),
        .pScissors     = scissors,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = MSAA_SAMPLE_COUNT,
        .sampleShadingEnable   = VK_TRUE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_TRUE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        {
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = rgba_mask,
        },
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = SDL_arraysize(color_blend_attachments),
        .pAttachments    = color_blend_attachments,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = SDL_arraysize(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .layout              = renderer.pipeline_layouts[SimpleRendering::Pipeline::Skybox],
        .renderPass          = renderer.render_pass,
        .subpass             = SimpleRendering::Pass::Skybox,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Pipeline::Skybox]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = load_shader("triangle_push.vert.spv"),
            .pName  = "main",
        },
        {

            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = load_shader("triangle_push.frag.spv"),
            .pName  = "main",
        },
    };

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
        },
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal)),
        },
        {
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord)),
        },
    };

    VkVertexInputBindingDescription vertex_binding_descriptions[] = {
        {
            .binding   = 0,
            .stride    = sizeof(TrianglesVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
        .pVertexBindingDescriptions      = vertex_binding_descriptions,
        .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
        .pVertexAttributeDescriptions    = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewports[] = {
        {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(ctx.extent2D.width),
            .height   = static_cast<float>(ctx.extent2D.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    VkRect2D scissors[] = {
        {
            .offset = {0, 0},
            .extent = ctx.extent2D,
        },
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = SDL_arraysize(viewports),
        .pViewports    = viewports,
        .scissorCount  = SDL_arraysize(scissors),
        .pScissors     = scissors,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_FRONT_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = MSAA_SAMPLE_COUNT,
        .sampleShadingEnable   = VK_TRUE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_TRUE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        {
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = rgba_mask,
        },
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = SDL_arraysize(color_blend_attachments),
        .pAttachments    = color_blend_attachments,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = SDL_arraysize(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .layout              = renderer.pipeline_layouts[SimpleRendering::Pipeline::Scene3D],
        .renderPass          = renderer.render_pass,
        .subpass             = SimpleRendering::Pass::Objects3D,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Pipeline::Scene3D]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = load_shader("colored_geometry.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = load_shader("colored_geometry.frag.spv"),
            .pName  = "main",
        },
    };

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
        },
    };

    VkVertexInputBindingDescription vertex_binding_descriptions[] = {
        {
            .binding   = 0,
            .stride    = sizeof(TrianglesVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
        .pVertexBindingDescriptions      = vertex_binding_descriptions,
        .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
        .pVertexAttributeDescriptions    = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewports[] = {
        {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(ctx.extent2D.width),
            .height   = static_cast<float>(ctx.extent2D.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    VkRect2D scissors[] = {
        {
            .offset = {0, 0},
            .extent = ctx.extent2D,
        },
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = SDL_arraysize(viewports),
        .pViewports    = viewports,
        .scissorCount  = SDL_arraysize(scissors),
        .pScissors     = scissors,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_FRONT_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = MSAA_SAMPLE_COUNT,
        .sampleShadingEnable   = VK_TRUE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_TRUE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        {
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = rgba_mask,
        },
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = SDL_arraysize(color_blend_attachments),
        .pAttachments    = color_blend_attachments,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = SDL_arraysize(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .layout              = renderer.pipeline_layouts[SimpleRendering::Pipeline::ColoredGeometry],
        .renderPass          = renderer.render_pass,
        .subpass             = SimpleRendering::Pass::Objects3D,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Pipeline::ColoredGeometry]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = load_shader("colored_geometry_skinned.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = load_shader("colored_geometry_skinned.frag.spv"),
            .pName  = "main",
        },
    };

    struct SkinnedVertex
    {
      vec3     position;
      vec3     normal;
      vec2     texcoord;
      uint16_t joint[4];
      vec4     weight;
    };

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, position)),
        },
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, normal)),
        },
        {
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, texcoord)),
        },
        {
            .location = 3,
            .binding  = 0,
            .format   = VK_FORMAT_R16G16B16A16_UINT,
            .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, joint)),
        },
        {
            .location = 4,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, weight)),
        },
    };

    VkVertexInputBindingDescription vertex_binding_descriptions[] = {
        {
            .binding   = 0,
            .stride    = sizeof(SkinnedVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
        .pVertexBindingDescriptions      = vertex_binding_descriptions,
        .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
        .pVertexAttributeDescriptions    = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewports[] = {
        {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(ctx.extent2D.width),
            .height   = static_cast<float>(ctx.extent2D.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    VkRect2D scissors[] = {
        {
            .offset = {0, 0},
            .extent = ctx.extent2D,
        },
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = SDL_arraysize(viewports),
        .pViewports    = viewports,
        .scissorCount  = SDL_arraysize(scissors),
        .pScissors     = scissors,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_FRONT_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = MSAA_SAMPLE_COUNT,
        .sampleShadingEnable   = VK_TRUE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_TRUE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        {
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = rgba_mask,
        },
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = SDL_arraysize(color_blend_attachments),
        .pAttachments    = color_blend_attachments,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = SDL_arraysize(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .layout              = renderer.pipeline_layouts[SimpleRendering::Pipeline::ColoredGeometrySkinned],
        .renderPass          = renderer.render_pass,
        .subpass             = SimpleRendering::Pass::Objects3D,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Pipeline::ColoredGeometrySkinned]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = load_shader("imgui.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = load_shader("imgui.frag.spv"),
            .pName  = "main",
        },
    };

    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, position)),
        },
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, tex_coord)),
        },
        {
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R8G8B8A8_UNORM,
            .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, color)),
        },
    };

    VkVertexInputBindingDescription vertex_binding_descriptions[] = {
        {
            .binding   = 0,
            .stride    = sizeof(ImguiVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
        .pVertexBindingDescriptions      = vertex_binding_descriptions,
        .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
        .pVertexAttributeDescriptions    = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewports[] = {
        {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(ctx.extent2D.width),
            .height   = static_cast<float>(ctx.extent2D.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        },
    };

    VkRect2D scissors[] = {
        {
            .offset = {0, 0},
            .extent = ctx.extent2D,
        },
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = SDL_arraysize(viewports),
        .pViewports    = viewports,
        .scissorCount  = SDL_arraysize(scissors),
        .pScissors     = scissors,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_NONE,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = MSAA_SAMPLE_COUNT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        {
            .blendEnable         = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = rgba_mask,
        },
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = SDL_arraysize(color_blend_attachments),
        .pAttachments    = color_blend_attachments,
    };

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT};

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = SDL_arraysize(dynamic_states),
        .pDynamicStates    = dynamic_states,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = SDL_arraysize(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = &dynamic_state,
        .layout              = renderer.pipeline_layouts[SimpleRendering::Pipeline::ImGui],
        .renderPass          = renderer.render_pass,
        .subpass             = SimpleRendering::Pass::ImGui,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Pipeline::ImGui]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

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

  {
    VkCommandBufferAllocateInfo alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = SDL_arraysize(renderer.secondary_command_buffers),
    };

    vkAllocateCommandBuffers(ctx.device, &alloc, renderer.secondary_command_buffers);
  }
}

void Engine::submit_simple_rendering(uint32_t image_index)
{
  VkCommandBuffer cmd = simple_rendering.primary_command_buffers[image_index];

  {
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);
  }

  VkClearValue clear_values[] = {
      {.color = {{0.0f, 0.0f, 0.2f, 1.0f}}},
      {.depthStencil = {1.0, 0}},
      {.color = {{0.0f, 0.0f, 0.2f, 1.0f}}},
      {.depthStencil = {1.0, 0}},
  };

  {
    VkRenderPassBeginInfo begin = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = simple_rendering.render_pass,
        .framebuffer     = simple_rendering.framebuffers[image_index],
        .renderArea      = {.extent = generic_handles.extent2D},
        .clearValueCount = SDL_arraysize(clear_values),
        .pClearValues    = clear_values,
    };

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  }

  int              secondary_stride_offset = Engine::SimpleRendering::Pipeline::Count * image_index;
  VkCommandBuffer* secondary_cbs           = &simple_rendering.secondary_command_buffers[secondary_stride_offset];

  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Pipeline::Skybox]);
  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Pipeline::Scene3D]);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Pipeline::ColoredGeometry]);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);
  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Pipeline::ImGui]);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &generic_handles.image_available,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &generic_handles.render_finished,
    };

    vkQueueSubmit(generic_handles.graphics_queue, 1, &submit, simple_rendering.submition_fences[image_index]);
  }

  {
    VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &generic_handles.render_finished,
        .swapchainCount     = 1,
        .pSwapchains        = &generic_handles.swapchain,
        .pImageIndices      = &image_index,
    };

    vkQueuePresentKHR(generic_handles.graphics_queue, &present);
  }
}
