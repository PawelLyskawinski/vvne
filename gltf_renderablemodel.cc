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

} // namespace

namespace gltf {

void RenderableModel::construct(Engine& engine, const Model& model) noexcept
{
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
  size_t host_vertex_buffer_size = 0;
  size_t host_index_buffer_size  = 0;

  uint8_t* uploadBuffer = engine.double_ended_stack.allocate_back<uint8_t>(fileSize);
  {
    // single mesh, single primitive
    const Primitive& primitive = model.meshes[0].primitives[0];

    int    count        = 0;
    int    vertexoffset = 0;
    float* positions    = nullptr;
    float* normals      = nullptr;
    float* texcoords    = nullptr;

    {
      const Accessor&   index_accessor = model.accessors[primitive.indices];
      const BufferView& index_view     = model.bufferViews[index_accessor.bufferView];

      vertexoffset  = index_view.byteLength;
      indices_count = static_cast<uint32_t>(index_accessor.count);
      indices_type  = VK_INDEX_TYPE_UINT32;

      switch (index_accessor.componentType)
      {
      case ACCESSOR_COMPONENTTYPE_UINT8:
        host_index_buffer_size = sizeof(uint8_t) * index_accessor.count;
        break;
      case ACCESSOR_COMPONENTTYPE_UINT16:
        host_index_buffer_size = sizeof(uint16_t) * index_accessor.count;
        indices_type           = VK_INDEX_TYPE_UINT16;
        break;
      case ACCESSOR_COMPONENTTYPE_UINT32:
        host_index_buffer_size = sizeof(uint32_t) * index_accessor.count;
        indices_type           = VK_INDEX_TYPE_UINT32;
        break;
      default:
        SDL_assert(false);
        break;
      }

      SDL_memcpy(uploadBuffer, &dataBuffer[index_view.byteOffset], host_index_buffer_size);
    }

    {
      const Accessor&   position_accessor = model.accessors[primitive.position_attrib];
      const BufferView& position_view     = model.bufferViews[position_accessor.bufferView];

      positions = reinterpret_cast<float*>(&dataBuffer[position_view.byteOffset]);
      count     = position_accessor.count;
    }

    {
      const Accessor&   normal_accessor = model.accessors[primitive.normal_attrib];
      const BufferView& normal_view     = model.bufferViews[normal_accessor.bufferView];

      normals = reinterpret_cast<float*>(&dataBuffer[normal_view.byteOffset]);
    }

    {
      const Accessor&   texcoord_accessor = model.accessors[primitive.texcoord_attrib];
      const BufferView& texcoord_view     = model.bufferViews[texcoord_accessor.bufferView];

      texcoords = reinterpret_cast<float*>(&dataBuffer[texcoord_view.byteOffset]);

      SDL_Log("componentType: %d, type: %d", texcoord_accessor.componentType, texcoord_accessor.type);
    }

    Vertex* vertices = reinterpret_cast<Vertex*>(&uploadBuffer[vertexoffset]);
    for (int i = 0; i < count; ++i)
    {
      Vertex* current      = &vertices[i];
      current->position[0] = positions[(3 * i) + 0];
      current->position[1] = positions[(3 * i) + 1];
      current->position[2] = positions[(3 * i) + 2];
      current->normal[0]   = normals[(3 * i) + 0];
      current->normal[1]   = normals[(3 * i) + 1];
      current->normal[2]   = normals[(3 * i) + 2];
      current->texcoord[0] = texcoords[(2 * i) + 0];
      current->texcoord[1] = texcoords[(2 * i) + 1];
    }

    host_vertex_buffer_size = count * sizeof(Vertex);
  }

  VkBuffer       host_buffer = VK_NULL_HANDLE;
  VkDeviceMemory host_memory = VK_NULL_HANDLE;

  {
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = fileSize;
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
  vkMapMemory(engine.generic_handles.device, host_memory, 0, fileSize, 0, (void**)&mapped_gpu_memory);
  SDL_memcpy(mapped_gpu_memory, uploadBuffer, fileSize);
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

  albedo_texture_idx = engine.load_texture(model.images[0].data);
}

} // namespace gltf
