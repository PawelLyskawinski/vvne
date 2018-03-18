#include "engine.hh"
#include <SDL2/SDL_assert.h>
#include <stb_image.h>

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

VkShaderModule engine_load_shader(Engine& engine, const char* filepath)
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
  vkCreateShaderModule(engine.device, &ci, nullptr, &result);
  SDL_free(buffer);

  return result;
}

int engine_load_texture(Engine& engine, const char* filepath)
{
  int x           = 0;
  int y           = 0;
  int real_format = 0;

  stbi_uc* pixels = stbi_load(filepath, &x, &y, &real_format, STBI_rgb_alpha);

  int    depth        = 0;
  int    pitch        = 0;
  Uint32 pixel_format = 0;

  depth        = 32;
  pitch        = 4 * x;
  pixel_format = SDL_PIXELFORMAT_RGBA32;

  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, x, y, depth, pitch, pixel_format);
  int          result  = engine_load_texture(engine, surface);
  SDL_FreeSurface(surface);

  stbi_image_free(pixels);
  return result;
}

int engine_load_texture(Engine &engine, SDL_Surface *surface)
{
  auto getSurfaceFormat = [](SDL_Surface* surface) -> VkFormat {
    switch (surface->format->BitsPerPixel)
    {
    default:
    case 32:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case 8:
      return VK_FORMAT_R8_UNORM;
    }
  };

  VkImage staging_image = VK_NULL_HANDLE;
  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = getSurfaceFormat(surface);
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

    vkCreateImage(engine.device, &ci, nullptr, &staging_image);
  }

  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(engine.physical_device, &properties);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(engine.device, staging_image, &reqs);

    VkMemoryPropertyFlags type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, type);

    vkAllocateMemory(engine.device, &allocate, nullptr, &staging_memory);
    vkBindImageMemory(engine.device, staging_image, staging_memory, 0);
  }

  VkSubresourceLayout subresource_layout{};
  VkImageSubresource  image_subresource{};
  image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vkGetImageSubresourceLayout(engine.device, staging_image, &image_subresource, &subresource_layout);

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

  vkMapMemory(engine.device, staging_memory, 0, device_size, 0, &mapped_data);

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

  vkUnmapMemory(engine.device, staging_memory);

  const int resultIdx = engine.loaded_textures;
  engine.loaded_textures += 1;
  VkImage&        result_image  = engine.images[resultIdx];
  VkDeviceMemory& result_memory = engine.images_memory[resultIdx];
  VkImageView&    result_view   = engine.image_views[resultIdx];

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = getSurfaceFormat(surface);
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

    vkCreateImage(engine.device, &ci, nullptr, &result_image);
  }

  {
    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(engine.physical_device, &properties);

    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(engine.device, result_image, &reqs);

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(engine.device, &allocate, nullptr, &result_memory);
    vkBindImageMemory(engine.device, result_image, result_memory, 0);
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
    ci.format           = getSurfaceFormat(surface);
    ci.image            = result_image;
    vkCreateImageView(engine.device, &ci, nullptr, &result_view);
  }

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool        = engine.graphics_command_pool;
    allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vkAllocateCommandBuffers(engine.device, &allocate, &command_buffer);
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
    vkCreateFence(engine.device, &ci, nullptr, &image_upload_fence);
  }

  {
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &command_buffer;
    vkQueueSubmit(engine.graphics_queue, 1, &submit, image_upload_fence);
  }

  vkWaitForFences(engine.device, 1, &image_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(engine.device, image_upload_fence, nullptr);
  vkFreeMemory(engine.device, staging_memory, nullptr);
  vkDestroyImage(engine.device, staging_image, nullptr);

  return resultIdx;
}
