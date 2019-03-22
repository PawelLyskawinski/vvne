#include "engine.hh"
#include "linmath.h"
#include "sha256.h"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <SDL2/SDL_timer.h>
#include <stb_image.h>
#pragma GCC diagnostic pop

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

const uint32_t initial_window_width  = 1200u;
const uint32_t initial_window_height = 900u;

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

const char* to_cstr(VkPresentModeKHR mode)
{
  const char* modes[] = {"IMMEDIATE", "MAILBOX (smart v-sync)", "FIFO (v-sync)", "FIFO RELAXED", "unknown?"};
  return modes[clamp(mode, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_RANGE_SIZE_KHR)];
}

} // namespace

void Engine::startup(bool vulkan_validation_enabled)
{
  permanent_stack.setup(HOST_PERMANENT_ALLOCATOR_POOL_SIZE);
  dirty_stack.setup(HOST_DIRTY_ALLOCATOR_POOL_SIZE);
  render_passes.init();

  window = SDL_CreateWindow("vvne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, initial_window_width,
                            initial_window_height, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

  {
    VkApplicationInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "vvne",
        .applicationVersion = 1,
        .pEngineName        = "vvne_engine",
        .engineVersion      = 1,
        .apiVersion         = VK_API_VERSION_1_0,
    };

    const char* instance_layers[] = {"VK_LAYER_LUNARG_standard_validation"};

    uint32_t count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr);
    const char** extensions = dirty_stack.alloc<const char*>(vulkan_validation_enabled ? count + 1 : count);
    SDL_Vulkan_GetInstanceExtensions(window, &count, extensions);
    if (vulkan_validation_enabled)
    {
      extensions[count] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
      count += 1;
    }

    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pApplicationInfo        = &ai,
        .enabledLayerCount       = vulkan_validation_enabled ? 1u : 0u,
        .ppEnabledLayerNames     = vulkan_validation_enabled ? instance_layers : nullptr,
        .enabledExtensionCount   = count,
        .ppEnabledExtensionNames = extensions,
    };

    vkCreateInstance(&ci, nullptr, &instance);
  }

  if (vulkan_validation_enabled)
  {
    VkDebugReportCallbackCreateInfoEXT ci = {
        .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .pNext       = nullptr,
        .flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
        .pfnCallback = vulkan_debug_callback,
    };

    auto fcn = (PFN_vkCreateDebugReportCallbackEXT)(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
    fcn(instance, &ci, nullptr, &debug_callback);
  }

  {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    VkPhysicalDevice* handles = dirty_stack.alloc<VkPhysicalDevice>(count);
    vkEnumeratePhysicalDevices(instance, &count, handles);

    physical_device = handles[0];
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
    SDL_Log("Selecting graphics card: %s", physical_device_properties.deviceName);
  }

  SDL_bool surface_result = SDL_Vulkan_CreateSurface(window, instance, &surface);
  if (SDL_FALSE == surface_result)
  {
    SDL_Log("%s", SDL_GetError());
    return;
  }

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);
  extent2D = surface_capabilities.currentExtent;

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    VkQueueFamilyProperties* all_properties = dirty_stack.alloc<VkQueueFamilyProperties>(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, all_properties);

    for (uint32_t i = 0; i < count; ++i)
    {
      VkQueueFamilyProperties properties          = all_properties[i];
      VkBool32                has_present_support = 0;
      vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &has_present_support);

      if (has_present_support && (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT))
      {
        graphics_family_index = i;
        break;
      }
    }
  }

  {
    const char* device_layers[]     = {"VK_LAYER_LUNARG_standard_validation"};
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    float       queue_priorities[]  = {1.0f};

    VkDeviceQueueCreateInfo graphics = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_family_index,
        .queueCount       = SDL_arraysize(queue_priorities),
        .pQueuePriorities = queue_priorities,
    };

    VkPhysicalDeviceFeatures device_features = {
        .sampleRateShading = VK_TRUE,
        .fillModeNonSolid  = VK_TRUE, // enables VK_POLYGON_MODE_LINE
        .wideLines         = VK_TRUE,
    };

    VkDeviceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &graphics,
        .enabledLayerCount       = vulkan_validation_enabled ? 1u : 0u,
        .ppEnabledLayerNames     = vulkan_validation_enabled ? device_layers : nullptr,
        .enabledExtensionCount   = SDL_arraysize(device_extensions),
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures        = &device_features,
    };

    vkCreateDevice(physical_device, &ci, nullptr, &device);
  }

  vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
    VkSurfaceFormatKHR* formats = dirty_stack.alloc<VkSurfaceFormatKHR>(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats);

    surface_format = formats[0];
    for (uint32_t i = 0; i < count; ++i)
    {
      if (VK_FORMAT_B8G8R8A8_UNORM != formats[i].format)
        continue;

      if (VK_COLOR_SPACE_SRGB_NONLINEAR_KHR != formats[i].colorSpace)
        continue;

      surface_format = formats[i];
      break;
    }
  }

  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
    VkPresentModeKHR* present_modes = dirty_stack.alloc<VkPresentModeKHR>(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, present_modes);

    SDL_Log("Supported presentation modes");
    for (uint32_t i = 0; i < count; ++i)
      SDL_Log("%s", to_cstr(present_modes[i]));

    auto has = [](const VkPresentModeKHR* all, uint32_t n, VkPresentModeKHR elem) {
      for (uint32_t i = 0; i < n; ++i)
        if (elem == all[i])
          return true;
      return false;
    };

    present_mode = has(present_modes, count, VK_PRESENT_MODE_IMMEDIATE_KHR)
                       ? VK_PRESENT_MODE_IMMEDIATE_KHR
                       : has(present_modes, count, VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                                : VK_PRESENT_MODE_FIFO_KHR;
  }

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
          .image            = swapchain_images[i],
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = surface_format.format,
          .components       = cm,
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

    vkCreateImage(device, &ci, nullptr, &msaa_color_image);
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

    vkCreateImage(device, &ci, nullptr, &depth_image);
  }

  {
    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {.width = SHADOWMAP_IMAGE_DIM, .height = SHADOWMAP_IMAGE_DIM, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = SHADOWMAP_CASCADE_COUNT,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCreateImage(device, &ci, nullptr, &shadowmap_image);
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

    vkCreateSampler(device, &ci, nullptr, &texture_sampler);
  }

  {
    VkSamplerCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_FALSE,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };

    vkCreateSampler(device, &ci, nullptr, &shadowmap_sampler);
  }

  // STATIC_GEOMETRY

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GPU_DEVICE_LOCAL_MEMORY_POOL_SIZE,
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

    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.device_local.memory);
    vkBindBufferMemory(device, gpu_device_local_memory_buffer, memory_blocks.device_local.memory, 0);
  }

  // STATIC_GEOMETRY_TRANSFER
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GPU_HOST_VISIBLE_TRANSFER_SOURCE_MEMORY_POOL_SIZE,
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

    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.host_visible_transfer_source.memory);
    vkBindBufferMemory(device, gpu_host_visible_transfer_source_memory_buffer,
                       memory_blocks.host_visible_transfer_source.memory, 0);
  }

  // HOST VISIBLE

  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GPU_HOST_COHERENT_MEMORY_POOL_SIZE,
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

    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.host_coherent.memory);
    vkBindBufferMemory(device, gpu_host_coherent_memory_buffer, memory_blocks.host_coherent.memory, 0);
  }

  // IMAGES
  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, depth_image, &reqs);
    memory_blocks.device_images.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE,
        .memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkAllocateMemory(device, &allocate, nullptr, &memory_blocks.device_images.memory);
    vkBindImageMemory(device, depth_image, memory_blocks.device_images.memory,
                      memory_blocks.device_images.stack_pointer);
    memory_blocks.device_images.stack_pointer += align(reqs.size, reqs.alignment);
  }

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, msaa_color_image, &reqs);
    vkBindImageMemory(device, msaa_color_image, memory_blocks.device_images.memory,
                      memory_blocks.device_images.stack_pointer);
    memory_blocks.device_images.stack_pointer += align(reqs.size, reqs.alignment);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, shadowmap_image, &reqs);
    vkBindImageMemory(device, shadowmap_image, memory_blocks.device_images.memory,
                      memory_blocks.device_images.stack_pointer);
    memory_blocks.device_images.stack_pointer += align(reqs.size, reqs.alignment);
  }

  // image views can only be created when memory is bound to the image handle

  if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
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
        .image            = msaa_color_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = surface_format.format,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &msaa_color_image_view);
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
        .image            = depth_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &depth_image_view);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = SHADOWMAP_CASCADE_COUNT,
    };

    VkComponentMapping comp = {
        .r = VK_COMPONENT_SWIZZLE_R,
        .g = VK_COMPONENT_SWIZZLE_G,
        .b = VK_COMPONENT_SWIZZLE_B,
        .a = VK_COMPONENT_SWIZZLE_A,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = shadowmap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &shadowmap_image_view);
  }

  for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
        .levelCount     = 1,
        .baseArrayLayer = static_cast<uint32_t>(cascade_idx),
        .layerCount     = 1,
    };

    VkComponentMapping comp = {
        .r = VK_COMPONENT_SWIZZLE_R,
        .g = VK_COMPONENT_SWIZZLE_G,
        .b = VK_COMPONENT_SWIZZLE_B,
        .a = VK_COMPONENT_SWIZZLE_A,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = shadowmap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .components       = comp,
        .subresourceRange = sr,
    };

    vkCreateImageView(device, &ci, nullptr, &shadowmap_cascade_image_views[cascade_idx]);
  }

  {
    autoclean_images.push(depth_image);
    autoclean_image_views.push(depth_image_view);

    if (VK_SAMPLE_COUNT_1_BIT != MSAA_SAMPLE_COUNT)
    {
      autoclean_images.push(msaa_color_image);
      autoclean_image_views.push(msaa_color_image_view);
    }

    autoclean_images.push(shadowmap_image);
    autoclean_image_views.push(shadowmap_image_view);

    for (auto& image_view : shadowmap_cascade_image_views)
      autoclean_image_views.push(image_view);
  }

  // UBO HOST VISIBLE
  {
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = GPU_HOST_COHERENT_UBO_MEMORY_POOL_SIZE,
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
              .image               = shadowmap_image,
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
              .image               = depth_image,
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

  dirty_stack.reset();

  setup_render_passes();
  setup_framebuffers();
  setup_descriptor_set_layouts();
  setup_pipeline_layouts();
  setup_pipelines();

  for (VkFence& submition_fence : submition_fences)
  {
    VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    vkCreateFence(device, &ci, nullptr, &submition_fence);
  }
}

void Engine::teardown()
{
  vkDeviceWaitIdle(device);

  descriptor_set_layouts.destroy(device);
  render_passes.destroy(device);
  pipelines.destroy(device);

  for (VkFence& fence : submition_fences)
    vkDestroyFence(device, fence, nullptr);

  for (auto& image : autoclean_images)
    vkDestroyImage(device, image, nullptr);

  for (auto& image_view : autoclean_image_views)
    vkDestroyImageView(device, image_view, nullptr);

  memory_blocks.destroy(device);

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

  for (VkImageView swapchain_image_view : swapchain_image_views)
    vkDestroyImageView(device, swapchain_image_view, nullptr);

  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  SDL_DestroyWindow(window);

  if (VK_NULL_HANDLE != debug_callback)
  {
    using Fcn = PFN_vkDestroyDebugReportCallbackEXT;
    auto fcn  = (Fcn)(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
    fcn(instance, debug_callback, nullptr);
  }

  vkDestroyInstance(instance, nullptr);

  dirty_stack.teardown();
  permanent_stack.teardown();
}

Texture Engine::load_texture(const char* filepath, bool register_for_destruction)
{
  int             x           = 0;
  int             y           = 0;
  int             real_format = 0;
  SDL_PixelFormat format      = {.format = SDL_PIXELFORMAT_RGBA32, .BitsPerPixel = 32, .BytesPerPixel = (32 + 7) / 8};
  stbi_uc*        pixels      = stbi_load(filepath, &x, &y, &real_format, STBI_rgb_alpha);

  SDL_assert(nullptr != pixels);

  SDL_Surface surface = {.format = &format, .w = x, .h = y, .pitch = 4 * x, .pixels = pixels};
  Texture     result  = load_texture(&surface, register_for_destruction);
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

VkFormat bitsPerPixelToFormat(SDL_Surface* surface) { return bitsPerPixelToFormat(surface->format->BitsPerPixel); }

} // namespace

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
    vkBindImageMemory(device, result.image, memory_blocks.device_images.memory,
                      memory_blocks.device_images.stack_pointer);
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
    vkBindImageMemory(device, result.image, memory_blocks.device_images.memory,
                      memory_blocks.device_images.stack_pointer);
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

VkShaderModule Engine::load_shader(const char* file_path)
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

  SDL_RWops* handle      = SDL_RWFromFile(&hash_string[54], "rb");
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
  vkCreateShaderModule(device, &ci, nullptr, &result);
  SDL_free(buffer);

  return result;
}

void Pipelines::destroy(VkDevice device)
{
  auto destroy = [device](const Pipelines::Pair& in) {
    vkDestroyPipeline(device, in.pipeline, nullptr);
    vkDestroyPipelineLayout(device, in.layout, nullptr);
  };

  destroy(shadowmap);
  destroy(skybox);
  destroy(scene3D);
  destroy(pbr_water);
  destroy(colored_geometry);
  destroy(colored_geometry_triangle_strip);
  destroy(colored_geometry_skinned);
  destroy(green_gui);
  destroy(green_gui_weapon_selector_box_left);
  destroy(green_gui_weapon_selector_box_right);
  destroy(green_gui_lines);
  destroy(green_gui_sdf_font);
  destroy(green_gui_triangle);
  destroy(green_gui_radar_dots);
  destroy(imgui);
  destroy(debug_billboard);
  destroy(colored_model_wireframe);
}

void RenderPasses::destroy(VkDevice device)
{
  shadowmap.destroy(device);
  skybox.destroy(device);
  color_and_depth.destroy(device);
  gui.destroy(device);
}

void RenderPasses::init()
{
  shadowmap.framebuffers             = shadowmap_framebuffers;
  shadowmap.framebuffers_count       = SDL_arraysize(shadowmap_framebuffers);
  skybox.framebuffers                = skybox_framebuffers;
  skybox.framebuffers_count          = SDL_arraysize(skybox_framebuffers);
  color_and_depth.framebuffers       = color_and_depth_framebuffers;
  color_and_depth.framebuffers_count = SDL_arraysize(color_and_depth_framebuffers);
  gui.framebuffers                   = gui_framebuffers;
  gui.framebuffers_count             = SDL_arraysize(gui_framebuffers);
}

void RenderPass::destroy(VkDevice device)
{
  vkDestroyRenderPass(device, render_pass, nullptr);
  for (uint32_t i = 0; i < framebuffers_count; ++i)
    vkDestroyFramebuffer(device, framebuffers[i], nullptr);
}

void RenderPass::begin(VkCommandBuffer cmd, uint32_t image_index)
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

void DescriptorSetLayouts::destroy(VkDevice device)
{
  auto destroy = [device](VkDescriptorSetLayout layout) { vkDestroyDescriptorSetLayout(device, layout, nullptr); };
  destroy(shadow_pass);
  destroy(pbr_metallic_workflow_material);
  destroy(pbr_ibl_cubemaps_and_brdf_lut);
  destroy(pbr_dynamic_lights);
  destroy(single_texture_in_frag);
  destroy(skinning_matrices);
  destroy(cascade_shadow_map_matrices_ubo_frag);
}

void MemoryBlocks::destroy(VkDevice device)
{
  auto destroy = [device](const GpuMemoryBlock& block) { vkFreeMemory(device, block.memory, nullptr); };
  destroy(device_local);
  destroy(host_visible_transfer_source);
  destroy(device_images);
  destroy(host_coherent);
  destroy(host_coherent_ubo);
}