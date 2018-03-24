#ifdef __linux__
#define VK_USE_PLATFORM_XCB_KHR
#else
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "engine.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_vulkan.h>

#define INITIAL_WINDOW_WIDTH 1200
#define INITIAL_WINDOW_HEIGHT 900

VkBool32
#ifndef __linux__
    __stdcall
#endif
    vulkan_debug_callback(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT, uint64_t, size_t, int32_t, const char*,
                          const char* msg, void*)
{
  SDL_Log("validation layer: %s\n", msg);
  return VK_FALSE;
}

void Engine::basic_startup()
{
  uint8_t small_stack[1024];

  {
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "cvk";
    ai.applicationVersion = 1;
    ai.pEngineName        = "cvk_engine";
    ai.engineVersion      = 1;
    ai.apiVersion         = VK_API_VERSION_1_0;

    const char* instance_layers[] = {
#ifndef __linux__
        "VK_LAYER_LUNARG_standard_validation"
#endif
    };
    const char* instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __linux__
                                         "VK_KHR_xlib_surface",
#else
                                         VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
                                         VK_EXT_DEBUG_REPORT_EXTENSION_NAME};

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.ppEnabledLayerNames     = instance_layers;
    ci.ppEnabledExtensionNames = instance_extensions;
    ci.enabledLayerCount       = SDL_arraysize(instance_layers);
    ci.enabledExtensionCount   = SDL_arraysize(instance_extensions);

    vkCreateInstance(&ci, nullptr, &instance);
  }

  {
    VkDebugReportCallbackCreateInfoEXT ci{};
    ci.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    ci.flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    ci.pfnCallback = vulkan_debug_callback;

    auto fcn =
        (PFN_vkCreateDebugReportCallbackEXT)(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
    fcn(instance, &ci, nullptr, &debug_callback);
  }

  window = SDL_CreateWindow("minimalistic VK engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

  {
    uint32_t          count   = 0;
    VkPhysicalDevice* handles = reinterpret_cast<VkPhysicalDevice*>(small_stack);

    vkEnumeratePhysicalDevices(instance, &count, nullptr);
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
    uint32_t                 count          = 0;
    VkQueueFamilyProperties* all_properties = reinterpret_cast<VkQueueFamilyProperties*>(small_stack);

    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, all_properties);

    for (uint32_t i = 0; count; ++i)
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

    VkDeviceQueueCreateInfo graphics{};
    graphics.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics.queueFamilyIndex = graphics_family_index;
    graphics.queueCount       = SDL_arraysize(queue_priorities);
    graphics.pQueuePriorities = queue_priorities;

    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &graphics;
    ci.enabledLayerCount       = SDL_arraysize(device_layers);
    ci.ppEnabledLayerNames     = device_layers;
    ci.enabledExtensionCount   = SDL_arraysize(device_extensions);
    ci.ppEnabledExtensionNames = device_extensions;
    ci.pEnabledFeatures        = &device_features;

    vkCreateDevice(physical_device, &ci, nullptr, &device);
  }

  vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

  {
    uint32_t            count   = 0;
    VkSurfaceFormatKHR* formats = reinterpret_cast<VkSurfaceFormatKHR*>(small_stack);

    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
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
    uint32_t count         = 0;
    auto*    present_modes = reinterpret_cast<VkPresentModeKHR*>(small_stack);

    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, present_modes);

    present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; ++i)
    {
      if (VK_PRESENT_MODE_MAILBOX_KHR == present_modes[i])
      {
        present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
    }
  }

  {
    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface;
    ci.minImageCount    = SWAPCHAIN_IMAGES_COUNT;
    ci.imageFormat      = surface_format.format;
    ci.imageColorSpace  = surface_format.colorSpace;
    ci.imageExtent      = extent2D;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = surface_capabilities.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = present_mode;
    ci.clipped          = VK_TRUE;

    vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);

    uint32_t swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, nullptr);
    SDL_assert(SWAPCHAIN_IMAGES_COUNT == swapchain_images_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, swapchain_images);
  }

  {
    VkComponentMapping cm{};
    cm.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    cm.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    cm.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    cm.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    VkImageSubresourceRange sr{};
    sr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.levelCount = 1;
    sr.layerCount = 1;

    for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      VkImageViewCreateInfo ci{};
      ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      ci.image            = swapchain_images[i];
      ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
      ci.format           = surface_format.format;
      ci.components       = cm;
      ci.subresourceRange = sr;

      vkCreateImageView(device, &ci, nullptr, &swapchain_image_views[i]);
    }
  }

  {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = graphics_family_index;

    vkCreateCommandPool(device, &ci, nullptr, &graphics_command_pool);
  }

  // Pool sizes below are just an suggestions. They have to be adjusted for the final release builds
  {
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 * SWAPCHAIN_IMAGES_COUNT},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 * SWAPCHAIN_IMAGES_COUNT}};

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = 10 * SWAPCHAIN_IMAGES_COUNT;
    ci.poolSizeCount = SDL_arraysize(pool_sizes);
    ci.pPoolSizes    = pool_sizes;

    vkCreateDescriptorPool(device, &ci, nullptr, &descriptor_pool);
  }

  {
    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device, &ci, nullptr, &image_available);
    vkCreateSemaphore(device, &ci, nullptr, &render_finished);
  }

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_D32_SFLOAT;
    ci.extent.width  = extent2D.width;
    ci.extent.height = extent2D.height;
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(device, &ci, nullptr, &depth_image);
  }

  {
    VkSamplerCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter               = VK_FILTER_LINEAR;
    ci.minFilter               = VK_FILTER_LINEAR;
    ci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.mipLodBias              = 0.0f;
    ci.anisotropyEnable        = VK_TRUE;
    ci.maxAnisotropy           = 1;
    ci.compareEnable           = VK_FALSE;
    ci.compareOp               = VK_COMPARE_OP_NEVER;
    ci.minLod                  = 0.0f;
    ci.maxLod                  = 1.0f;
    ci.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.unnormalizedCoordinates = VK_FALSE;

    for (VkSampler& sampler : texture_samplers)
      vkCreateSampler(device, &ci, nullptr, &sampler);
  }

  {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo alloc{};
      alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      alloc.commandPool        = graphics_command_pool;
      alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      alloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &alloc, &command_buffer);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(command_buffer, &begin);
    }

    {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.dstAccessMask =
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
      barrier.image                       = depth_image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(command_buffer);

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &command_buffer;
      vkQueueSubmit(graphics_queue, 1, &submit, VK_NULL_HANDLE);
    }

    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device, graphics_command_pool, 1, &command_buffer);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(device, depth_image, &reqs);

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &allocate, nullptr, &depth_image_memory);
    vkBindImageMemory(device, depth_image, depth_image_memory, 0);
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sr.levelCount = 1;
    sr.layerCount = 1;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image            = depth_image;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ci.format           = VK_FORMAT_D32_SFLOAT;
    ci.subresourceRange = sr;

    vkCreateImageView(device, &ci, nullptr, &depth_image_view);
  }
}
