#include "gltf.hh"
#include "stb_image.h"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_timer.h>
#include <linmath.h>

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

namespace gltf {

void RenderableModel::loadGLB(Engine& engine, const char* path) noexcept
{
  uint64_t start = SDL_GetPerformanceCounter();

  Engine::DoubleEndedStack& stack            = engine.double_ended_stack;
  SDL_RWops*                ctx              = SDL_RWFromFile(path, "rb");
  int                       glb_file_size    = static_cast<int>(SDL_RWsize(ctx));
  uint8_t*                  glb_file_content = stack.allocate_back<uint8_t>(glb_file_size);

  SDL_RWread(ctx, glb_file_content, sizeof(char), static_cast<size_t>(glb_file_size));
  SDL_RWclose(ctx);

  const uint32_t offset_to_chunk_data = 2 * sizeof(uint32_t);
  const uint32_t offset_to_json       = 3 * sizeof(uint32_t); // glb header
  const uint32_t json_chunk_length    = *reinterpret_cast<uint32_t*>(&glb_file_content[offset_to_json]);
  const char*    json_data = reinterpret_cast<const char*>(&glb_file_content[offset_to_json + offset_to_chunk_data]);
  const uint32_t offset_to_binary = offset_to_json + offset_to_chunk_data + json_chunk_length;
  const uint8_t* binary_data      = &glb_file_content[offset_to_binary + offset_to_chunk_data];

  struct Seeker
  {
    const char*    data;
    const uint32_t length;

    Seeker node(const char* name) const
    {
      const int name_length   = static_cast<const int>(SDL_strlen(name));
      int       iter          = 0;
      int       open_brackets = 0;

      while (iter < (length - name_length))
      {
        switch (data[iter])
        {
        case '{':
        case '[':
          open_brackets += 1;
          break;
        case '}':
        case ']':
          open_brackets -= 1;
          break;
        case ':':
        case '"':
        case ',':
        case '.':
          break;
        default:
          if ((0 == open_brackets) and (0 == SDL_memcmp(&data[iter], name, static_cast<size_t>(name_length))))
          {
            const char* node_begin  = &data[iter + name_length + 4]; // 3 is from \":{
            uint32_t    node_length = 0;
            open_brackets           = 2;

            while (open_brackets)
            {
              switch (node_begin[node_length])
              {
              case '{':
              case '[':
                open_brackets += 1;
                break;
              case '}':
              case ']':
                open_brackets -= 1;
                break;
              default:
                break;
              }

              node_length += 1;
            }

            return {node_begin, node_length};
          }
          break;
        }

        iter += 1;
      }

      return {nullptr, 0};
    }

    bool has(const char* name) const
    {
      return (nullptr != node(name).data);
    }

    Seeker idx(const int desired_array_element) const
    {
      int iter          = 0;
      int open_brackets = 1;
      int array_element = 0;

      while (iter < length)
      {
        if (desired_array_element == array_element)
        {
          int offset = (0 < array_element) ? 3 : 0;
          return {&data[iter + offset], length - (iter + 1)};
        }

        switch (data[iter])
        {
        case '{':
        case '[':
          open_brackets += 1;
          break;
        case '}':
        case ']':
          open_brackets -= 1;
          if (0 == open_brackets)
            array_element += 1;
          break;
        default:
          break;
        }

        iter += 1;
      }

      return {nullptr, 0};
    }

    int integer(const char* name) const
    {
      const int name_length   = static_cast<const int>(SDL_strlen(name));
      int       iter          = 0;
      int       open_brackets = 0;

      while (iter < (length - name_length))
      {
        switch (data[iter])
        {
        case '{':
        case '[':
          open_brackets += 1;
          break;
        case '}':
        case ']':
          open_brackets -= 1;
          break;
        case ':':
        case '"':
        case ',':
        case '.':
          break;
        default:
          if ((0 == open_brackets) and (0 == SDL_memcmp(&data[iter], name, static_cast<size_t>(name_length))))
            return static_cast<int>(SDL_strtol(&data[iter + name_length + 2], nullptr, 10));
          break;
        }

        iter += 1;
      }

      return 0;
    }
  };

  const Seeker document             = Seeker{&json_data[1], json_chunk_length};
  const Seeker primitives           = document.node("meshes").idx(0).node("primitives");
  const Seeker first_attrib         = primitives.node("attributes").idx(0);
  const int    indices              = primitives.integer("indices");
  const int    position             = first_attrib.integer("POSITION");
  const int    normal               = first_attrib.integer("NORMAL");
  const int    texcoord             = first_attrib.integer("TEXCOORD_0");
  const Seeker accessors            = document.node("accessors");
  const Seeker index_accessor       = accessors.idx(indices);
  const Seeker position_accessor    = accessors.idx(position);
  const int    index_type           = index_accessor.integer("componentType");
  const int    index_buffer_view    = index_accessor.integer("bufferView");
  const int    position_count       = position_accessor.integer("count");
  const int    position_buffer_view = position_accessor.integer("bufferView");
  const Seeker buffer_views         = document.node("bufferViews");

  enum IndexType
  {
    UINT8  = 5121,
    UINT16 = 5123,
    UINT32 = 5125,
  };

  struct Vertex
  {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
  };

  const bool is_index_type_uint16 = (IndexType::UINT16 == index_type);

  indices_count = static_cast<uint32_t>(index_accessor.integer("count"));
  indices_type  = is_index_type_uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

  const int required_index_space     = indices_count * (is_index_type_uint16 ? sizeof(uint16_t) : sizeof(uint32_t));
  const int required_vertex_space    = position_count * sizeof(Vertex);
  const int total_upload_buffer_size = required_index_space + required_vertex_space;
  uint8_t*  upload_buffer            = engine.double_ended_stack.allocate_back<uint8_t>(total_upload_buffer_size);
  const int index_buffer_glb_offset  = buffer_views.idx(index_buffer_view).integer("byteOffset");

  SDL_memset(upload_buffer, 0, total_upload_buffer_size);

  if (IndexType::UINT8 == index_type)
  {
    uint32_t*      dst = reinterpret_cast<uint32_t*>(upload_buffer);
    const uint8_t* src = &binary_data[index_buffer_glb_offset];

    for (int i = 0; i < indices_count; ++i)
      dst[i] = src[i];
  }
  else if (IndexType::UINT16 == index_type)
  {
    uint16_t*       dst = reinterpret_cast<uint16_t*>(upload_buffer);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(&binary_data[index_buffer_glb_offset]);

    for (int i = 0; i < indices_count; ++i)
      dst[i] = src[i];
  }
  else
  {
    SDL_memcpy(upload_buffer, &binary_data[index_buffer_glb_offset], static_cast<size_t>(required_index_space));
  }

  {
    Seeker    buffer_view         = buffer_views.idx(position_buffer_view);
    const int view_glb_offset     = buffer_view.integer("byteOffset");
    const int accessor_glb_offset = position_accessor.integer("byteOffset");
    const int start_offset        = view_glb_offset + accessor_glb_offset;
    Vertex*   dst_vertices        = reinterpret_cast<Vertex*>(&upload_buffer[required_index_space]);

    int stride = buffer_view.integer("stride");
    stride     = stride ? stride : sizeof(vec3);

    for (int i = 0; i < position_count; ++i)
    {
      Vertex&      current = dst_vertices[i];
      float*       dst     = current.position;
      const float* src     = reinterpret_cast<const float*>(&binary_data[start_offset + (stride * i)]);

      for (int j = 0; j < 3; ++j)
        dst[j] = src[j];
    }
  }

  {
    Seeker    accessor            = accessors.idx(normal);
    Seeker    buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
    const int view_glb_offset     = buffer_view.integer("byteOffset");
    const int accessor_glb_offset = accessor.integer("byteOffset");
    const int start_offset        = view_glb_offset + accessor_glb_offset;
    Vertex*   dst_vertices        = reinterpret_cast<Vertex*>(&upload_buffer[required_index_space]);

    int stride = buffer_view.integer("stride");
    stride     = stride ? stride : sizeof(vec3);

    for (int i = 0; i < position_count; ++i)
    {
      Vertex&      current = dst_vertices[i];
      float*       dst     = current.normal;
      const float* src     = reinterpret_cast<const float*>(&binary_data[start_offset + (stride * i)]);

      for (int j = 0; j < 3; ++j)
        dst[j] = src[j];
    }
  }

  if (first_attrib.has("TEXCOORD_0"))
  {
    Seeker    accessor            = accessors.idx(texcoord);
    Seeker    buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
    const int view_glb_offset     = buffer_view.integer("byteOffset");
    const int accessor_glb_offset = accessor.integer("byteOffset");
    const int start_offset        = view_glb_offset + accessor_glb_offset;
    Vertex*   dst_vertices        = reinterpret_cast<Vertex*>(&upload_buffer[required_index_space]);

    int stride = buffer_view.integer("stride");
    stride     = stride ? stride : sizeof(vec2);

    for (int i = 0; i < position_count; ++i)
    {
      Vertex&      current = dst_vertices[i];
      float*       dst     = current.texcoord;
      const float* src     = reinterpret_cast<const float*>(&binary_data[start_offset + (stride * i)]);

      for (int j = 0; j < 2; ++j)
        dst[j] = src[j];
    }
  }

  VkDeviceSize host_buffer_offset =
      engine.gpu_static_transfer.allocate(static_cast<VkDeviceSize>(total_upload_buffer_size));

  {
    uint8_t* mapped_gpu_memory = nullptr;
    vkMapMemory(engine.generic_handles.device, engine.gpu_static_transfer.memory, host_buffer_offset,
                total_upload_buffer_size, 0, (void**)&mapped_gpu_memory);
    SDL_memcpy(mapped_gpu_memory, upload_buffer, static_cast<size_t>(total_upload_buffer_size));
    vkUnmapMemory(engine.generic_handles.device, engine.gpu_static_transfer.memory);
  }

  indices_offset  = engine.gpu_static_geometry.allocate(required_index_space);
  vertices_offset = engine.gpu_static_geometry.allocate(required_vertex_space);

  VkCommandBuffer cmd = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool        = engine.generic_handles.graphics_command_pool;
    allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vkAllocateCommandBuffers(engine.generic_handles.device, &allocate, &cmd);
  }

  {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
  }

  {
    VkBufferCopy copies[2] = {};

    copies[0].size      = static_cast<VkDeviceSize>(required_index_space);
    copies[0].srcOffset = 0;
    copies[0].dstOffset = indices_offset;

    copies[1].size      = static_cast<VkDeviceSize>(required_vertex_space);
    copies[1].srcOffset = static_cast<VkDeviceSize>(required_index_space);
    copies[1].dstOffset = vertices_offset;

    vkCmdCopyBuffer(cmd, engine.gpu_static_transfer.buffer, engine.gpu_static_geometry.buffer, SDL_arraysize(copies),
                    copies);
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
    barriers[0].size                = static_cast<VkDeviceSize>(required_index_space);

    barriers[1].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer              = engine.gpu_static_geometry.buffer;
    barriers[1].offset              = vertices_offset;
    barriers[1].size                = static_cast<VkDeviceSize>(required_vertex_space);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                         SDL_arraysize(barriers), barriers, 0, nullptr);
  }

  vkEndCommandBuffer(cmd);

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
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(engine.generic_handles.graphics_queue, 1, &submit, data_upload_fence);
  }

  vkWaitForFences(engine.generic_handles.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(engine.generic_handles.device, data_upload_fence, nullptr);
  vkFreeCommandBuffers(engine.generic_handles.device, engine.generic_handles.graphics_command_pool, 1, &cmd);
  engine.gpu_static_transfer.pop();

  auto load_texture = [&engine, binary_data](Seeker buffer_view) -> int {
    int      offset      = buffer_view.integer("byteOffset");
    int      length      = buffer_view.integer("byteLength");
    int      x           = 0;
    int      y           = 0;
    int      real_format = 0;
    stbi_uc* pixels      = stbi_load_from_memory(&binary_data[offset], length, &x, &y, &real_format, STBI_rgb_alpha);

    int          depth        = 32;
    int          pitch        = 4 * x;
    Uint32       pixel_format = SDL_PIXELFORMAT_RGBA32;
    SDL_Surface* surface      = SDL_CreateRGBSurfaceWithFormatFrom(pixels, x, y, depth, pitch, pixel_format);

    int result = engine.load_texture(surface);

    SDL_FreeSurface(surface);
    stbi_image_free(pixels);
    return result;
  };

  Seeker images = document.node("images");
  if (0 < images.data)
  {
    albedo_texture_idx          = load_texture(buffer_views.idx(images.idx(0).integer("bufferView")));
    metal_roughness_texture_idx = load_texture(buffer_views.idx(images.idx(1).integer("bufferView")));
    emissive_texture_idx        = load_texture(buffer_views.idx(images.idx(2).integer("bufferView")));
    AO_texture_idx              = load_texture(buffer_views.idx(images.idx(3).integer("bufferView")));
    normal_texture_idx          = load_texture(buffer_views.idx(images.idx(4).integer("bufferView")));
  }

  stack.reset_back();

  uint64_t duration_ticks = SDL_GetPerformanceCounter() - start;
  float    elapsed_ms     = 1000.0f * ((float)duration_ticks / (float)SDL_GetPerformanceFrequency());
  SDL_Log("parsing GLB took: %.4f ms", elapsed_ms);
}

} // namespace gltf
