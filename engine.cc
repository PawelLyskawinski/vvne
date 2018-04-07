#ifdef __linux__
#define VK_USE_PLATFORM_XCB_KHR
#else
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "engine.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <stb_image.h>
#pragma GCC diagnostic pop

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

    vkCreateInstance(&ci, nullptr, &ctx.instance);
  }

  {
    VkDebugReportCallbackCreateInfoEXT ci{};
    ci.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    ci.flags       = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    ci.pfnCallback = vulkan_debug_callback;

    auto fcn = (PFN_vkCreateDebugReportCallbackEXT)(
        vkGetInstanceProcAddr(generic_handles.instance, "vkCreateDebugReportCallbackEXT"));
    fcn(generic_handles.instance, &ci, nullptr, &generic_handles.debug_callback);
  }

  ctx.window = SDL_CreateWindow("minimalistic VK engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);

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
    const char* device_layers[]     = {"VK_LAYER_LUNARG_standard_validation"};
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    float       queue_priorities[]  = {1.0f};

    VkDeviceQueueCreateInfo graphics{};
    graphics.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics.queueFamilyIndex = ctx.graphics_family_index;
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
    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface;
    ci.minImageCount    = SWAPCHAIN_IMAGES_COUNT;
    ci.imageFormat      = ctx.surface_format.format;
    ci.imageColorSpace  = ctx.surface_format.colorSpace;
    ci.imageExtent      = ctx.extent2D;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = ctx.surface_capabilities.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = ctx.present_mode;
    ci.clipped          = VK_TRUE;

    vkCreateSwapchainKHR(ctx.device, &ci, nullptr, &ctx.swapchain);

    uint32_t swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &swapchain_images_count, nullptr);
    SDL_assert(SWAPCHAIN_IMAGES_COUNT == swapchain_images_count);
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &swapchain_images_count, ctx.swapchain_images);
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
      ci.image            = ctx.swapchain_images[i];
      ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
      ci.format           = ctx.surface_format.format;
      ci.components       = cm;
      ci.subresourceRange = sr;

      vkCreateImageView(ctx.device, &ci, nullptr, &ctx.swapchain_image_views[i]);
    }
  }

  {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = ctx.graphics_family_index;

    vkCreateCommandPool(ctx.device, &ci, nullptr, &ctx.graphics_command_pool);
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

    vkCreateDescriptorPool(ctx.device, &ci, nullptr, &ctx.descriptor_pool);
  }

  {
    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(ctx.device, &ci, nullptr, &ctx.image_available);
    vkCreateSemaphore(ctx.device, &ci, nullptr, &ctx.render_finished);
  }

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_D32_SFLOAT;
    ci.extent.width  = ctx.extent2D.width;
    ci.extent.height = ctx.extent2D.height;
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(ctx.device, &ci, nullptr, &ctx.depth_image);
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

    for (VkSampler& sampler : ctx.texture_samplers)
      vkCreateSampler(ctx.device, &ci, nullptr, &sampler);
  }

  {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo alloc{};
      alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      alloc.commandPool        = ctx.graphics_command_pool;
      alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      alloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(ctx.device, &alloc, &command_buffer);
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
      barrier.image                       = ctx.depth_image;
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
      vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    }

    vkQueueWaitIdle(ctx.graphics_queue);
    vkFreeCommandBuffers(ctx.device, ctx.graphics_command_pool, 1, &command_buffer);
  }

  // STATIC_GEOMETRY

  {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size  = GpuStaticGeometry::MAX_MEMORY_SIZE;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(ctx.device, &ci, nullptr, &gpu_static_geometry.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, gpu_static_geometry.buffer, &reqs);
    gpu_static_geometry.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device, &allocate, nullptr, &gpu_static_geometry.memory);
    vkBindBufferMemory(ctx.device, gpu_static_geometry.buffer, gpu_static_geometry.memory, 0);
  }

  // HOST VISIBLE

  {
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = GpuHostVisible::MAX_MEMORY_SIZE;
    ci.usage       = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(ctx.device, &ci, nullptr, &gpu_host_visible.buffer);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(ctx.device, gpu_host_visible.buffer, &reqs);
    gpu_host_visible.alignment = reqs.alignment;

    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
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

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = Images::MAX_MEMORY_SIZE;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device, &allocate, nullptr, &images.memory);

    vkBindImageMemory(ctx.device, ctx.depth_image, images.memory, 0);
    images.used_memory += reqs.size;
  }

  images.images      = double_ended_stack.allocate_front<VkImage>(Images::MAX_COUNT);
  images.image_views = double_ended_stack.allocate_front<VkImageView>(Images::MAX_COUNT);

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    sr.levelCount = 1;
    sr.layerCount = 1;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image            = ctx.depth_image;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ci.format           = VK_FORMAT_D32_SFLOAT;
    ci.subresourceRange = sr;

    vkCreateImageView(ctx.device, &ci, nullptr, &ctx.depth_image_view);
  }

  double_ended_stack.reset_back();
  setup_simple_rendering();
}

void Engine::print_memory_statistics()
{
  float image_percent = 100.0f * ((float)images.used_memory / (float)Images::MAX_MEMORY_SIZE);
  float dv_percent    = 100.0f * ((float)gpu_static_geometry.used_memory / (float)GpuStaticGeometry::MAX_MEMORY_SIZE);
  float hv_percent    = 100.0f * ((float)gpu_host_visible.used_memory / (float)GpuHostVisible::MAX_MEMORY_SIZE);

  SDL_Log("### Memory statistics ###");
  SDL_Log("Image memory:           %.2f proc. out of %u MB", image_percent, Images::MAX_MEMORY_SIZE_MB);
  SDL_Log("device-visible memory:  %.2f proc. out of %u MB", dv_percent, GpuStaticGeometry::MAX_MEMORY_SIZE_MB);
  SDL_Log("host-visible memory:    %.2f proc. out of %u MB", hv_percent, GpuHostVisible::MAX_MEMORY_SIZE_MB);
}

void Engine::teardown()
{
  GenericHandles& ctx = generic_handles;

  vkDeviceWaitIdle(ctx.device);

  vkDestroyRenderPass(ctx.device, simple_rendering.render_pass, nullptr);
  for (auto& layout : simple_rendering.descriptor_set_layouts)
    vkDestroyDescriptorSetLayout(ctx.device, layout, nullptr);
  for (auto& framebuffer : simple_rendering.framebuffers)
    vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
  for (auto& layout : simple_rendering.pipeline_layouts)
    vkDestroyPipelineLayout(ctx.device, layout, nullptr);
  for (auto& pipeline : simple_rendering.pipelines)
    vkDestroyPipeline(ctx.device, pipeline, nullptr);
  for (auto& fence : simple_rendering.submition_fences)
    vkDestroyFence(ctx.device, fence, nullptr);

  for (uint32_t i = 0; i < images.loaded_count; ++i)
  {
    vkDestroyImage(ctx.device, images.images[i], nullptr);
  }

  for (uint32_t i = 0; i < images.loaded_count; ++i)
  {
    vkDestroyImageView(ctx.device, images.image_views[i], nullptr);
  }

  vkFreeMemory(ctx.device, images.memory, nullptr);
  vkFreeMemory(ctx.device, gpu_host_visible.memory, nullptr);
  vkFreeMemory(ctx.device, gpu_static_geometry.memory, nullptr);

  vkDestroyBuffer(ctx.device, gpu_host_visible.buffer, nullptr);
  vkDestroyBuffer(ctx.device, gpu_static_geometry.buffer, nullptr);

  vkDestroyImage(ctx.device, ctx.depth_image, nullptr);
  vkDestroyImageView(ctx.device, ctx.depth_image_view, nullptr);

  for (VkSampler& sampler : ctx.texture_samplers)
  {
    vkDestroySampler(ctx.device, sampler, nullptr);
  }

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

  using Fcn = PFN_vkDestroyDebugReportCallbackEXT;
  auto fcn  = (Fcn)(vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugReportCallbackEXT"));
  fcn(ctx.instance, ctx.debug_callback, nullptr);
  vkDestroyInstance(ctx.instance, nullptr);
}

namespace {

bool doesStringEndWith(const char* source, const char* ending)
{
  size_t source_length = SDL_strlen(source);
  size_t ending_length = SDL_strlen(ending);
  return 0 == SDL_strcmp(&source[source_length - ending_length], ending);
}

} // namespace

int Engine::load_texture(const char* filepath)
{
  int x           = 0;
  int y           = 0;
  int real_format = 0;
  int result      = 0;

  if (doesStringEndWith(filepath, ".hdr"))
  {
    SDL_Log("loading HDR file! %s", filepath);

    float*       pixels       = stbi_loadf(filepath, &x, &y, &real_format, 0);
    int          depth        = 32;
    int          pitch        = 4 * x;
    Uint32       pixel_format = SDL_PIXELFORMAT_RGBA32;
    SDL_Surface* surface      = SDL_CreateRGBSurfaceWithFormatFrom(pixels, x, y, depth, pitch, pixel_format);

    result = load_texture(surface);

    SDL_FreeSurface(surface);
    stbi_image_free(pixels);
  }
  else
  {
    stbi_uc*     pixels       = stbi_load(filepath, &x, &y, &real_format, STBI_rgb_alpha);
    int          depth        = 32;
    int          pitch        = 4 * x;
    Uint32       pixel_format = SDL_PIXELFORMAT_RGBA32;
    SDL_Surface* surface      = SDL_CreateRGBSurfaceWithFormatFrom(pixels, x, y, depth, pitch, pixel_format);

    result = load_texture(surface);

    SDL_FreeSurface(surface);
    stbi_image_free(pixels);
  }

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

int Engine::load_texture(SDL_Surface* surface)
{
  GenericHandles& ctx            = generic_handles;
  VkImage         staging_image  = VK_NULL_HANDLE;
  VkDeviceMemory  staging_memory = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = bitsPerPixelToFormat(surface);
    ci.extent.width  = static_cast<uint32_t>(surface->w);
    ci.extent.height = static_cast<uint32_t>(surface->h);
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_LINEAR;
    ci.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(ctx.device, &ci, nullptr, &staging_image);
  }

  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &properties);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(ctx.device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, type);

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
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = bitsPerPixelToFormat(surface);
    ci.extent.width  = texture_width;
    ci.extent.height = texture_height;
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(ctx.device, &ci, nullptr, &result_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(ctx.device, result_image, &reqs);
    vkBindImageMemory(ctx.device, result_image, images.memory, images.allocate(reqs.size));
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = 0;
    sr.layerCount     = 1;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ci.subresourceRange = sr;
    ci.format           = bitsPerPixelToFormat(surface);
    ci.image            = result_image;
    vkCreateImageView(ctx.device, &ci, nullptr, &result_view);
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool        = ctx.graphics_command_pool;
    allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vkAllocateCommandBuffers(ctx.device, &allocate, &command_buffer);
  }

  {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin);
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = 0;
    sr.layerCount     = 1;

    VkImageMemoryBarrier barriers[2] = {};

    barriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
    barriers[0].dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED;
    barriers[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image               = staging_image;
    barriers[0].subresourceRange    = sr;

    barriers[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
    barriers[1].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED;
    barriers[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image               = result_image;
    barriers[1].subresourceRange    = sr;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, SDL_arraysize(barriers), barriers);
  }

  {
    VkImageSubresourceLayers sl{};
    sl.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sl.mipLevel       = 0;
    sl.baseArrayLayer = 0;
    sl.layerCount     = 1;

    VkImageCopy copy{};
    copy.srcSubresource = sl;
    copy.dstSubresource = sl;
    copy.extent.width   = texture_width;
    copy.extent.height  = texture_height;
    copy.extent.depth   = 1;

    vkCmdCopyImage(command_buffer, staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = 0;
    sr.layerCount     = 1;

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = result_image;
    barrier.subresourceRange    = sr;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence image_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(ctx.device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &command_buffer;
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

VkShaderModule Engine::load_shader(const char* filepath)
{
  SDL_RWops* handle     = SDL_RWFromFile(filepath, "rb");
  uint32_t   filelength = static_cast<uint32_t>(SDL_RWsize(handle));
  uint8_t*   buffer     = static_cast<uint8_t*>(SDL_malloc(filelength));

  SDL_RWread(handle, buffer, sizeof(uint8_t), filelength);
  SDL_RWclose(handle);

  VkShaderModuleCreateInfo ci{};
  ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = filelength;
  ci.pCode    = reinterpret_cast<uint32_t*>(buffer);

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
    VkAttachmentDescription attachments[2] = {};

    attachments[0].format         = ctx.surface_format.format;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_reference{};
    depth_reference.attachment = 1;
    depth_reference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpasses[SimpleRendering::Passes::Count] = {};

    {
      VkSubpassDescription& subpass = subpasses[SimpleRendering::Passes::Skybox];
      subpass.pipelineBindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount  = 1;
      subpass.pColorAttachments     = &color_reference;
    }

    {
      VkSubpassDescription& subpass   = subpasses[SimpleRendering::Passes::Scene3D];
      subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount    = 1;
      subpass.pColorAttachments       = &color_reference;
      subpass.pDepthStencilAttachment = &depth_reference;
    }

    {
      VkSubpassDescription& subpass = subpasses[SimpleRendering::Passes::ImGui];
      subpass.pipelineBindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount  = 1;
      subpass.pColorAttachments     = &color_reference;
    }

    VkSubpassDependency dependencies[SimpleRendering::Passes::Count] = {};

    {
      VkSubpassDependency& dep = dependencies[SimpleRendering::Passes::Skybox];
      dep.srcSubpass           = VK_SUBPASS_EXTERNAL;
      dep.dstSubpass           = 0;
      dep.srcStageMask         = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dep.dstStageMask         = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dep.srcAccessMask        = VK_ACCESS_MEMORY_READ_BIT;
      dep.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    {
      VkSubpassDependency& dep = dependencies[SimpleRendering::Passes::Scene3D];
      dep.srcSubpass           = 0;
      dep.dstSubpass           = 1;
      dep.srcStageMask         = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dep.dstStageMask         = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dep.srcAccessMask        = VK_ACCESS_MEMORY_READ_BIT;
      dep.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    {
      VkSubpassDependency& dep = dependencies[SimpleRendering::Passes::ImGui];
      dep.srcSubpass           = 1;
      dep.dstSubpass           = 2;
      dep.srcStageMask         = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dep.dstStageMask         = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dep.srcAccessMask        = VK_ACCESS_MEMORY_READ_BIT;
      dep.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    ci.subpassCount    = SDL_arraysize(subpasses);
    ci.pSubpasses      = subpasses;
    ci.dependencyCount = SDL_arraysize(dependencies);
    ci.pDependencies   = dependencies;

    vkCreateRenderPass(ctx.device, &ci, nullptr, &renderer.render_pass);
  }

  {
    VkDescriptorSetLayoutBinding bindings[1] = {};

    bindings[0].binding         = 1;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = SDL_arraysize(bindings);
    ci.pBindings    = bindings;

    for (VkDescriptorSetLayout& layout : renderer.descriptor_set_layouts)
      vkCreateDescriptorSetLayout(ctx.device, &ci, nullptr, &layout);
  }

  {
    VkPushConstantRange ranges[2] = {};

    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = 16 * sizeof(float);

    ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ranges[1].offset     = 16 * sizeof(float);
    ranges[1].size       = 2 * sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = SWAPCHAIN_IMAGES_COUNT;
    ci.pSetLayouts            = renderer.descriptor_set_layouts;
    ci.pushConstantRangeCount = SDL_arraysize(ranges);
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Passes::Skybox]);
  }

  {
    VkPushConstantRange ranges[1] = {};

    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = 16 * sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = SWAPCHAIN_IMAGES_COUNT;
    ci.pSetLayouts            = renderer.descriptor_set_layouts;
    ci.pushConstantRangeCount = SDL_arraysize(ranges);
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Passes::Scene3D]);
  }

  {
    VkPushConstantRange ranges[1] = {};

    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = 16 * sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = SWAPCHAIN_IMAGES_COUNT;
    ci.pSetLayouts            = renderer.descriptor_set_layouts;
    ci.pushConstantRangeCount = SDL_arraysize(ranges);
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(ctx.device, &ci, nullptr, &renderer.pipeline_layouts[SimpleRendering::Passes::ImGui]);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = load_shader("skybox.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = load_shader("skybox.frag.spv");
    shader_stages[1].pName  = "main";

    VkVertexInputAttributeDescription attribute_descriptions[3] = {};

    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position));

    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[1].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal));

    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[2].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord));

    VkVertexInputBindingDescription vertex_binding_descriptions[1] = {};

    vertex_binding_descriptions[0].binding   = 0;
    vertex_binding_descriptions[0].stride    = sizeof(TrianglesVertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions);
    vertex_input_state.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state.vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions);
    vertex_input_state.pVertexAttributeDescriptions    = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewports[1] = {};

    viewports[0].x        = 0.0f;
    viewports[0].y        = 0.0f;
    viewports[0].width    = static_cast<float>(ctx.extent2D.width);
    viewports[0].height   = static_cast<float>(ctx.extent2D.height);
    viewports[0].minDepth = 0.0f;
    viewports[0].maxDepth = 1.0f;

    VkRect2D scissors[1] = {};

    scissors[0].offset = {0, 0};
    scissors[0].extent = ctx.extent2D;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = SDL_arraysize(viewports);
    viewport_state.pViewports    = viewports;
    viewport_state.scissorCount  = SDL_arraysize(scissors);
    viewport_state.pScissors     = scissors;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable       = VK_TRUE;
    depth_stencil_state.depthWriteEnable      = VK_TRUE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};

    color_blend_attachments[0].blendEnable         = VK_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = SDL_arraysize(color_blend_attachments);
    color_blend_state.pAttachments    = color_blend_attachments;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = SDL_arraysize(shader_stages);
    ci.pStages             = shader_stages;
    ci.pVertexInputState   = &vertex_input_state;
    ci.pInputAssemblyState = &input_assembly_state;
    ci.pViewportState      = &viewport_state;
    ci.pRasterizationState = &rasterization_state;
    ci.pMultisampleState   = &multisample_state;
    ci.pDepthStencilState  = &depth_stencil_state;
    ci.pColorBlendState    = &color_blend_state;
    ci.layout              = renderer.pipeline_layouts[SimpleRendering::Passes::Skybox];
    ci.renderPass          = renderer.render_pass;
    ci.subpass             = SimpleRendering::Passes::Skybox;
    ci.basePipelineHandle  = VK_NULL_HANDLE;
    ci.basePipelineIndex   = -1;
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Passes::Skybox]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = load_shader("triangle_push.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = load_shader("triangle_push.frag.spv");
    shader_stages[1].pName  = "main";

    VkVertexInputAttributeDescription attribute_descriptions[3] = {};

    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position));

    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[1].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal));

    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[2].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord));

    VkVertexInputBindingDescription vertex_binding_descriptions[1] = {};

    vertex_binding_descriptions[0].binding   = 0;
    vertex_binding_descriptions[0].stride    = sizeof(TrianglesVertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions);
    vertex_input_state.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state.vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions);
    vertex_input_state.pVertexAttributeDescriptions    = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewports[1] = {};

    viewports[0].x        = 0.0f;
    viewports[0].y        = 0.0f;
    viewports[0].width    = static_cast<float>(ctx.extent2D.width);
    viewports[0].height   = static_cast<float>(ctx.extent2D.height);
    viewports[0].minDepth = 0.0f;
    viewports[0].maxDepth = 1.0f;

    VkRect2D scissors[1] = {};

    scissors[0].offset = {0, 0};
    scissors[0].extent = ctx.extent2D;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = SDL_arraysize(viewports);
    viewport_state.pViewports    = viewports;
    viewport_state.scissorCount  = SDL_arraysize(scissors);
    viewport_state.pScissors     = scissors;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable       = VK_TRUE;
    depth_stencil_state.depthWriteEnable      = VK_TRUE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};

    color_blend_attachments[0].blendEnable         = VK_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = SDL_arraysize(color_blend_attachments);
    color_blend_state.pAttachments    = color_blend_attachments;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = SDL_arraysize(shader_stages);
    ci.pStages             = shader_stages;
    ci.pVertexInputState   = &vertex_input_state;
    ci.pInputAssemblyState = &input_assembly_state;
    ci.pViewportState      = &viewport_state;
    ci.pRasterizationState = &rasterization_state;
    ci.pMultisampleState   = &multisample_state;
    ci.pDepthStencilState  = &depth_stencil_state;
    ci.pColorBlendState    = &color_blend_state;
    ci.layout              = renderer.pipeline_layouts[SimpleRendering::Passes::Scene3D];
    ci.renderPass          = renderer.render_pass;
    ci.subpass             = SimpleRendering::Passes::Scene3D;
    ci.basePipelineHandle  = VK_NULL_HANDLE;
    ci.basePipelineIndex   = -1;
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Passes::Scene3D]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = load_shader("imgui.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = load_shader("imgui.frag.spv");
    shader_stages[1].pName  = "main";

    VkVertexInputAttributeDescription attribute_descriptions[3] = {};

    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[0].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, position));

    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[1].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, tex_coord));

    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].format   = VK_FORMAT_R8G8B8A8_UNORM;
    attribute_descriptions[2].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, color));

    VkVertexInputBindingDescription vertex_binding_descriptions[1] = {};

    vertex_binding_descriptions[0].binding   = 0;
    vertex_binding_descriptions[0].stride    = sizeof(ImguiVertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions);
    vertex_input_state.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state.vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions);
    vertex_input_state.pVertexAttributeDescriptions    = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewports[1] = {};

    viewports[0].x        = 0.0f;
    viewports[0].y        = 0.0f;
    viewports[0].width    = static_cast<float>(ctx.extent2D.width);
    viewports[0].height   = static_cast<float>(ctx.extent2D.height);
    viewports[0].minDepth = 0.0f;
    viewports[0].maxDepth = 1.0f;

    VkRect2D scissors[1] = {};

    scissors[0].offset = {0, 0};
    scissors[0].extent = ctx.extent2D;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = SDL_arraysize(viewports);
    viewport_state.pViewports    = viewports;
    viewport_state.scissorCount  = SDL_arraysize(scissors);
    viewport_state.pScissors     = scissors;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};

    color_blend_attachments[0].blendEnable         = VK_TRUE;
    color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = SDL_arraysize(color_blend_attachments);
    color_blend_state.pAttachments    = color_blend_attachments;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = SDL_arraysize(dynamic_states);
    dynamic_state.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = SDL_arraysize(shader_stages);
    ci.pStages             = shader_stages;
    ci.pVertexInputState   = &vertex_input_state;
    ci.pInputAssemblyState = &input_assembly_state;
    ci.pViewportState      = &viewport_state;
    ci.pRasterizationState = &rasterization_state;
    ci.pMultisampleState   = &multisample_state;
    ci.pColorBlendState    = &color_blend_state;
    ci.pDynamicState       = &dynamic_state;
    ci.layout              = renderer.pipeline_layouts[SimpleRendering::Passes::ImGui];
    ci.renderPass          = renderer.render_pass;
    ci.subpass             = SimpleRendering::Passes::ImGui;
    ci.basePipelineHandle  = VK_NULL_HANDLE;
    ci.basePipelineIndex   = -1;
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                              &renderer.pipelines[SimpleRendering::Passes::ImGui]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(ctx.device, shader_stage.module, nullptr);
  }

  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments[] = {ctx.swapchain_image_views[i], ctx.depth_image_view};

    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = renderer.render_pass;
    ci.width           = ctx.extent2D.width;
    ci.height          = ctx.extent2D.height;
    ci.layers          = 1;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    vkCreateFramebuffer(ctx.device, &ci, nullptr, &renderer.framebuffers[i]);
  }

  for (auto& submition_fence : renderer.submition_fences)
  {
    VkFenceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(ctx.device, &ci, nullptr, &submition_fence);
  }

  {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = ctx.graphics_command_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = SDL_arraysize(renderer.primary_command_buffers);
    vkAllocateCommandBuffers(ctx.device, &alloc, renderer.primary_command_buffers);
  }

  {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = ctx.graphics_command_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    alloc.commandBufferCount = SDL_arraysize(renderer.secondary_command_buffers);
    vkAllocateCommandBuffers(ctx.device, &alloc, renderer.secondary_command_buffers);
  }
}

void Engine::submit_simple_rendering(uint32_t image_index)
{
  VkCommandBuffer cmd = simple_rendering.primary_command_buffers[image_index];

  {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin);
  }

  VkClearValue color_clear = {};
  {
    float clear[] = {0.0f, 0.0f, 0.2f, 1.0f};
    SDL_memcpy(color_clear.color.float32, clear, sizeof(clear));
  }

  VkClearValue depth_clear{};
  depth_clear.depthStencil.depth = 1.0;

  VkClearValue clear_values[] = {color_clear, depth_clear};

  {
    VkRenderPassBeginInfo begin{};
    begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass        = simple_rendering.render_pass;
    begin.framebuffer       = simple_rendering.framebuffers[image_index];
    begin.clearValueCount   = SDL_arraysize(clear_values);
    begin.pClearValues      = clear_values;
    begin.renderArea.extent = generic_handles.extent2D;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  }

  int              secondary_stride_offset = Engine::SimpleRendering::Passes::Count * image_index;
  VkCommandBuffer* secondary_cbs           = &simple_rendering.secondary_command_buffers[secondary_stride_offset];

  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Passes::Skybox]);
  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Passes::Scene3D]);
  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  vkCmdExecuteCommands(cmd, 1, &secondary_cbs[Engine::SimpleRendering::Passes::ImGui]);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo         submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &generic_handles.image_available;
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &generic_handles.render_finished;
    vkQueueSubmit(generic_handles.graphics_queue, 1, &submit, simple_rendering.submition_fences[image_index]);
  }

  {
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &generic_handles.render_finished;
    present.swapchainCount     = 1;
    present.pSwapchains        = &generic_handles.swapchain;
    present.pImageIndices      = &image_index;
    vkQueuePresentKHR(generic_handles.graphics_queue, &present);
  }
}
