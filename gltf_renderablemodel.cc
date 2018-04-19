#include "gltf.hh"
#include "stb_image.h"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>

namespace {

template <typename T> T align(const T value, const T alignment)
{
  if (value % alignment)
  {
    return value + alignment - (value % alignment);
  }
  else
  {
    return value;
  }
}

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

VkIndexType gltfToVulkanIndexType(int in)
{
  switch (in)
  {
  case ACCESSOR_COMPONENTTYPE_UINT16:
    return VK_INDEX_TYPE_UINT16;
  default:
  case ACCESSOR_COMPONENTTYPE_UINT8:
  case ACCESSOR_COMPONENTTYPE_UINT32:
    return VK_INDEX_TYPE_UINT32;
  }
}

size_t vulkanIndexTypeToSize(VkIndexType in)
{
  switch (in)
  {
  case VK_INDEX_TYPE_UINT16:
    return sizeof(uint16_t);
  default:
  case VK_INDEX_TYPE_UINT32:
    return sizeof(uint32_t);
  }
}

} // namespace

namespace gltf {

void RenderableModel::construct(Engine& engine, const Model& model) noexcept
{
  SDL_Log("reading %s", model.buffers[0].path.data);

  SDL_RWops* ctx        = SDL_RWFromFile(model.buffers[0].path.data, "rb");
  size_t     fileSize   = static_cast<size_t>(SDL_RWsize(ctx));
  uint8_t*   dataBuffer = engine.double_ended_stack.allocate_back<uint8_t>(fileSize);
  SDL_RWread(ctx, dataBuffer, sizeof(char), fileSize);
  SDL_RWclose(ctx);

  struct Vertex
  {
    float position[3];
    float normal[3];
    float texcoord[2];
  };

  //
  // Reorganize data to fit into expected shader vertex layout
  //
  size_t   host_vertex_buffer_size  = 0;
  size_t   host_index_buffer_size   = 0;
  size_t   total_upload_buffer_size = 0;
  uint8_t* uploadBuffer             = nullptr;

  {
    // single mesh, single primitive
    const Primitive& primitive                     = model.meshes[0].primitives[0];
    int              upload_buffer_vertices_offset = 0;

    // calculate memory size for the upload buffer
    {
      Accessor index_accessor = model.accessors[primitive.indices];
      indices_type            = gltfToVulkanIndexType(index_accessor.componentType);
      host_index_buffer_size  = vulkanIndexTypeToSize(indices_type) * index_accessor.count;
    }

    {
      Accessor position_accessor = model.accessors[primitive.position_attrib];
      host_vertex_buffer_size    = position_accessor.count * sizeof(Vertex);
    }

    total_upload_buffer_size = host_index_buffer_size + host_vertex_buffer_size;
    uploadBuffer             = engine.double_ended_stack.allocate_back<uint8_t>(total_upload_buffer_size);
    SDL_memset(uploadBuffer, 0, fileSize);

    // index data will be re-arranged to be at start of upload buffer (wherever it might be in source buffer)
    {
      const Accessor&   index_accessor = model.accessors[primitive.indices];
      const BufferView& index_view     = model.bufferViews[index_accessor.bufferView];

      upload_buffer_vertices_offset = index_view.byteLength;
      indices_count                 = static_cast<uint32_t>(index_accessor.count);
      indices_type                  = gltfToVulkanIndexType(index_accessor.componentType);
      host_index_buffer_size        = vulkanIndexTypeToSize(indices_type) * index_accessor.count;

      // @todo: uint8 indices should be probably mapped to uint16 here
      // @todo: remove the branch below and optimize
      if (ACCESSOR_COMPONENTTYPE_UINT8 == index_accessor.componentType)
      {
        upload_buffer_vertices_offset = 4 * index_view.byteLength;

        uint32_t* dst = reinterpret_cast<uint32_t*>(uploadBuffer);
        uint8_t*  src = &dataBuffer[index_view.byteOffset];
        for (int i = 0; i < index_accessor.count; ++i)
        {
          dst[i] = src[i];
        }
      }
      else
      {
        SDL_memcpy(uploadBuffer, &dataBuffer[index_view.byteOffset], host_index_buffer_size);
      }
    }

    // write position data into upload buffer
    if (Primitive::Flag::hasPositionAttrib & primitive.flags)
    {
      Accessor   accessor    = model.accessors[primitive.position_attrib];
      BufferView buffer_view = model.bufferViews[accessor.bufferView];
      Vertex*    vertices    = reinterpret_cast<Vertex*>(&uploadBuffer[upload_buffer_vertices_offset]);

      host_vertex_buffer_size = accessor.count * sizeof(Vertex);

      int start_offset = (buffer_view.byteOffset + accessor.byteOffset);
      int vec3_size    = 3 * sizeof(float);
      int stride       = (buffer_view.flags & BufferView::Flag::hasByteStride) ? buffer_view.byteStride : vec3_size;

      for (int i = 0; i < accessor.count; ++i)
      {
        Vertex& current = vertices[i];
        float*  src     = reinterpret_cast<float*>(&dataBuffer[start_offset + (stride * i)]);
        SDL_memcpy(current.position, src, vec3_size);
      }
    }

    // write normal data into upload buffer
    if (Primitive::Flag::hasNormalAttrib & primitive.flags)
    {
      Accessor   accessor    = model.accessors[primitive.normal_attrib];
      BufferView buffer_view = model.bufferViews[accessor.bufferView];
      Vertex*    vertices    = reinterpret_cast<Vertex*>(&uploadBuffer[upload_buffer_vertices_offset]);

      int start_offset = (buffer_view.byteOffset + accessor.byteOffset);
      int vec3_size    = 3 * sizeof(float);
      int stride       = (buffer_view.flags & BufferView::Flag::hasByteStride) ? buffer_view.byteStride : vec3_size;

      for (int i = 0; i < accessor.count; ++i)
      {
        Vertex& current = vertices[i];
        float*  src     = reinterpret_cast<float*>(&dataBuffer[start_offset + (stride * i)]);
        SDL_memcpy(current.normal, src, vec3_size);
      }
    }

    // write normal data into upload buffer
    if (Primitive::Flag::hasTexcoordAttrib & primitive.flags)
    {
      Accessor   accessor    = model.accessors[primitive.texcoord_attrib];
      BufferView buffer_view = model.bufferViews[accessor.bufferView];
      Vertex*    vertices    = reinterpret_cast<Vertex*>(&uploadBuffer[upload_buffer_vertices_offset]);

      int start_offset = (buffer_view.byteOffset + accessor.byteOffset);
      int vec2_size    = 2 * sizeof(float);
      int stride       = (buffer_view.flags & BufferView::Flag::hasByteStride) ? buffer_view.byteStride : vec2_size;

      for (int i = 0; i < accessor.count; ++i)
      {
        Vertex& current = vertices[i];
        float*  src     = reinterpret_cast<float*>(&dataBuffer[start_offset + (stride * i)]);
        SDL_memcpy(current.texcoord, src, vec2_size);
      }
    }
  }

  VkBuffer       host_buffer = VK_NULL_HANDLE;
  VkDeviceMemory host_memory = VK_NULL_HANDLE;

  {
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = total_upload_buffer_size;
    ci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(engine.generic_handles.device, &ci, nullptr, &host_buffer);

    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(engine.generic_handles.device, host_buffer, &reqs);

    VkPhysicalDeviceMemoryProperties properties = {};
    vkGetPhysicalDeviceMemoryProperties(engine.generic_handles.physical_device, &properties);

    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocate{};
    allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize  = reqs.size;
    allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, flags);

    vkAllocateMemory(engine.generic_handles.device, &allocate, nullptr, &host_memory);
    vkBindBufferMemory(engine.generic_handles.device, host_buffer, host_memory, 0);
  }

  uint8_t* mapped_gpu_memory = nullptr;
  vkMapMemory(engine.generic_handles.device, host_memory, 0, total_upload_buffer_size, 0, (void**)&mapped_gpu_memory);
  SDL_memcpy(mapped_gpu_memory, uploadBuffer, total_upload_buffer_size);
  vkUnmapMemory(engine.generic_handles.device, host_memory);

  indices_offset  = engine.gpu_static_geometry.allocate(host_index_buffer_size);
  vertices_offset = engine.gpu_static_geometry.allocate(host_vertex_buffer_size);

  VkCommandBuffer copy_command = VK_NULL_HANDLE;
  {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool        = engine.generic_handles.graphics_command_pool;
    allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vkAllocateCommandBuffers(engine.generic_handles.device, &allocate, &copy_command);
  }

  {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(copy_command, &begin);
  }

  {
    VkBufferCopy copies[2] = {};

    copies[0].size      = host_index_buffer_size;
    copies[0].srcOffset = 0;
    copies[0].dstOffset = indices_offset;

    copies[1].size      = host_vertex_buffer_size;
    copies[1].srcOffset = host_index_buffer_size;
    copies[1].dstOffset = vertices_offset;

    vkCmdCopyBuffer(copy_command, host_buffer, engine.gpu_static_geometry.buffer, SDL_arraysize(copies), copies);
  }

  {
    VkBufferMemoryBarrier barriers[2] = {};

    barriers[0].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].buffer              = engine.gpu_static_geometry.buffer;
    barriers[0].offset              = indices_offset;
    barriers[0].size                = host_index_buffer_size;

    barriers[1].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer              = engine.gpu_static_geometry.buffer;
    barriers[1].offset              = vertices_offset;
    barriers[1].size                = host_vertex_buffer_size;

    vkCmdPipelineBarrier(copy_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                         nullptr, SDL_arraysize(barriers), barriers, 0, nullptr);
  }

  vkEndCommandBuffer(copy_command);

  VkFence data_upload_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(engine.generic_handles.device, &ci, nullptr, &data_upload_fence);
  }

  {
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &copy_command;
    vkQueueSubmit(engine.generic_handles.graphics_queue, 1, &submit, data_upload_fence);
  }

  vkWaitForFences(engine.generic_handles.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(engine.generic_handles.device, data_upload_fence, nullptr);
  vkFreeCommandBuffers(engine.generic_handles.device, engine.generic_handles.graphics_command_pool, 1, &copy_command);

  vkDestroyBuffer(engine.generic_handles.device, host_buffer, nullptr);
  vkFreeMemory(engine.generic_handles.device, host_memory, nullptr);

  SDL_Log("images count: %d", model.images.size());

  if (0 != model.images.n)
  {
    albedo_texture_idx          = engine.load_texture(model.images[0].data);
    metal_roughness_texture_idx = engine.load_texture(model.images[1].data);
    emissive_texture_idx        = engine.load_texture(model.images[2].data);
    AO_texture_idx              = engine.load_texture(model.images[3].data);
    normal_texture_idx          = engine.load_texture(model.images[4].data);
  }
}

} // namespace gltf
