#include "gltf.hh"
#include "stb_image.h"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_timer.h>
#include <linmath.h>

#ifndef __linux__
#include <stdlib.h>
#endif

namespace {

size_t find_substring_idx(const char* big_string, const size_t big_string_length, const char* small_string)
{
  size_t result              = 0;
  size_t small_string_length = SDL_strlen(small_string);

  if (small_string_length > big_string_length)
    return 0;

  for (size_t i = 0; i < (big_string_length - small_string_length); ++i)
  {
    if (0 == SDL_memcmp(&big_string[i], small_string, small_string_length))
    {
      result = i;
      break;
    }
  }

  return result;
}

bool is_open_bracket(char in) { return ('{' == in) or ('[' == in); }

bool is_closing_bracket(char in) { return ('}' == in) or (']' == in); }

struct Seeker
{
  const char* data;
  size_t      length;

  Seeker node(const char* name) const
  {
    const size_t name_length = SDL_strlen(name);
    const char*  iter        = data;

    while ('{' != *iter)
      ++iter;
    ++iter;

    int  open_brackets = 1;
    char character     = *iter;

    while (1 <= open_brackets)
    {
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }
      else if ((1 == open_brackets) and ('"' == character))
      {
        if (0 == SDL_memcmp(&iter[1], name, name_length))
        {
          return {iter, length - (iter - data)};
        }
      }

      ++iter;
      character = *iter;
    }

    return *this;
  }

  bool has(const char* name) const
  {
    const char* iter = data;
    while ('{' != *iter)
      ++iter;
    ++iter;

    int open_brackets = 1;
    while ((1 <= open_brackets) and (iter != &data[length]))
    {
      char character = *iter;
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }

      ++iter;
    }

    size_t sublen = (iter - data);
    return (0 != find_substring_idx(data, sublen, name));
  }

  Seeker idx(const int desired_array_element) const
  {
    Seeker result{};

    const char* iter = data;
    while ('[' != *iter)
      ++iter;
    ++iter;

    if (0 != desired_array_element)
    {
      int open_brackets = 1;
      int array_element = 0;

      while (array_element != desired_array_element)
      {
        char character = *iter;
        if (is_open_bracket(character))
        {
          open_brackets += 1;
        }
        else if (is_closing_bracket(character))
        {
          open_brackets -= 1;
        }
        else if ((1 == open_brackets) and (',' == character))
        {
          array_element += 1;
        }

        ++iter;
      }
    }

    result.data   = iter;
    result.length = length - (iter - data);

    return result;
  }

  int idx_integer(const int desired_array_element) const
  {
    Seeker element = idx(desired_array_element);
    long   result  = SDL_strtol(element.data, nullptr, 10);
    return static_cast<int>(result);
  }

  float idx_float(const int desired_array_element) const
  {
    Seeker element = idx(desired_array_element);

#ifdef __linux__
    double result = SDL_strtod(element.data, nullptr);
#else
    // Apperently SDL_strtod on windows does not read scientific
    // notation correctly when not compiled with correct compilation
    // flag.
    double result = strtod(element.data, nullptr);
#endif

    return static_cast<float>(result);
  }

  int elements_count() const
  {
    const char* iter = data;
    while ('[' != *iter)
      ++iter;
    ++iter;

    int  result        = 1;
    int  open_brackets = 1;
    char character     = *iter;

    while (1 <= open_brackets)
    {
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }
      else if ((1 == open_brackets) and (',' == character))
      {
        result += 1;
      }

      ++iter;
      character = *iter;
    }

    return result;
  }

  int integer(const char* name) const
  {
    const size_t idx = find_substring_idx(data, length, name);

    const char* iter = &data[idx];
    while (':' != *iter)
      ++iter;
    ++iter;

    long result = SDL_strtol(iter, nullptr, 10);
    return static_cast<int>(result);
  }
};

class SceneGraphAllocator
{
public:
  SceneGraphAllocator(Seeker document_node, Stack& allocator)
      : document_node(document_node)
      , allocator(allocator)
  {
  }

  template <typename T> SceneGraphAllocator& allocate(ArrayView<T>& arrayview, const char* name)
  {
    if (document_node.has(name))
    {
      arrayview.alloc(allocator, document_node.node(name).elements_count());
    }

    return *this;
  }

private:
  Seeker document_node;
  Stack& allocator;
};

class MaterialTextureLoader
{
public:
  MaterialTextureLoader(Engine& engine, const uint8_t binary_data[], const Seeker& material_json,
                        const Seeker& images_json, const Seeker& buffer_views_json)
      : engine(engine)
      , binary_data(binary_data)
      , material_json(material_json)
      , images_json(images_json)
      , buffer_views_json(buffer_views_json)
  {
  }

  MaterialTextureLoader& load(Texture& result, const char* name)
  {
    int    image_idx       = material_json.node(name).integer("index");
    int    buffer_view_idx = images_json.idx(image_idx).integer("bufferView");
    Seeker buffer_view     = buffer_views_json.idx(buffer_view_idx);
    int    offset          = buffer_view.integer("byteOffset");
    int    length          = buffer_view.integer("byteLength");
    int    x               = 0;
    int    y               = 0;
    int    real_format     = 0;

    SDL_PixelFormat format  = {.format = SDL_PIXELFORMAT_RGBA32, .BitsPerPixel = 32, .BytesPerPixel = (32 + 7) / 8};
    stbi_uc*        pixels  = stbi_load_from_memory(&binary_data[offset], length, &x, &y, &real_format, STBI_rgb_alpha);
    SDL_Surface     surface = {.format = &format, .w = x, .h = y, .pitch = 4 * x, .pixels = pixels};
    result                  = engine.load_texture(&surface);
    stbi_image_free(pixels);
    return *this;
  }

  MaterialTextureLoader& replace_material(Seeker new_material_json)
  {
    material_json = new_material_json;
    return *this;
  }

private:
  Engine&        engine;
  const uint8_t* binary_data;
  Seeker         material_json;
  Seeker         images_json;
  Seeker         buffer_views_json;
}; // namespace

} // namespace

SceneGraph loadGLB(Engine& engine, const char* path)
{
  uint64_t start = SDL_GetPerformanceCounter();

  SDL_RWops* ctx              = SDL_RWFromFile(path, "rb");
  uint64_t   glb_file_size    = static_cast<uint64_t>(SDL_RWsize(ctx));
  uint8_t*   glb_file_content = engine.dirty_stack.alloc<uint8_t>(glb_file_size);

  SDL_RWread(ctx, glb_file_content, sizeof(char), static_cast<size_t>(glb_file_size));
  SDL_RWclose(ctx);

  const uint32_t offset_to_chunk_data = 2 * sizeof(uint32_t);
  const uint32_t offset_to_json       = 3 * sizeof(uint32_t); // glb header
  const uint32_t json_chunk_length    = *reinterpret_cast<uint32_t*>(&glb_file_content[offset_to_json]);
  const char*    json_data = reinterpret_cast<const char*>(&glb_file_content[offset_to_json + offset_to_chunk_data]);
  const uint32_t offset_to_binary = offset_to_json + offset_to_chunk_data + json_chunk_length;
  const uint8_t* binary_data      = &glb_file_content[offset_to_binary + offset_to_chunk_data];

  const Seeker document     = Seeker{json_data, json_chunk_length};
  const Seeker buffer_views = document.node("bufferViews");

  SceneGraph scene_graph = {};

  SceneGraphAllocator(document, engine.permanent_stack)
      .allocate(scene_graph.materials, "materials")
      .allocate(scene_graph.meshes, "meshes")
      .allocate(scene_graph.nodes, "nodes")
      .allocate(scene_graph.scenes, "scenes")
      .allocate(scene_graph.animations, "animations")
      .allocate(scene_graph.skins, "skins");

  // ---------------------------------------------------------------------------
  // MATERIALS
  // ---------------------------------------------------------------------------

  if (document.has("images"))
  {
    Seeker images = document.node("images");

    for (int material_idx = 0; material_idx < scene_graph.materials.count; ++material_idx)
    {
      Material& material      = scene_graph.materials.data[material_idx];
      Seeker    material_json = document.node("materials").idx(material_idx);

      MaterialTextureLoader(engine, binary_data, material_json, images, buffer_views)
          .load(material.emissive_texture, "emissiveTexture")
          .load(material.AO_texture, "occlusionTexture")
          .load(material.normal_texture, "normalTexture")
          .replace_material(material_json.node("pbrMetallicRoughness"))
          .load(material.albedo_texture, "baseColorTexture")
          .load(material.metal_roughness_texture, "metallicRoughnessTexture");
    }
  }

  // ---------------------------------------------------------------------------
  // MESHES
  // ---------------------------------------------------------------------------

  Seeker accessors = document.node("accessors");
  for (int mesh_idx = 0; mesh_idx < scene_graph.meshes.count; ++mesh_idx)
  {
    Mesh&  mesh      = scene_graph.meshes.data[mesh_idx];
    Seeker mesh_json = document.node("meshes").idx(mesh_idx);

    // For now we'll be using single primitive per mesh. I can't think of any situation when multiple primitives will
    // be used. Maybe some gltf converters / generators do this? @todo implement in the future if nessesary
    Seeker primitive  = mesh_json.node("primitives").idx(0);
    mesh.material     = primitive.integer("material");
    Seeker attributes = primitive.node("attributes");

    int    indices_accessor_idx = primitive.integer("indices");
    Seeker index_accessor       = accessors.idx(indices_accessor_idx);
    int    index_type           = index_accessor.integer("componentType");
    int    index_buffer_view    = index_accessor.integer("bufferView");

    Seeker position_accessor    = accessors.idx(attributes.integer("POSITION"));
    int    position_count       = position_accessor.integer("count");
    int    position_buffer_view = position_accessor.integer("bufferView");

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

    struct SkinnedVertex
    {
      vec3     position;
      vec3     normal;
      vec2     texcoord;
      uint16_t joint[4];
      vec4     weight;
    };

    const bool is_index_type_uint16 = (IndexType::UINT16 == index_type);

    mesh.indices_count = static_cast<uint32_t>(index_accessor.integer("count"));
    mesh.indices_type  = is_index_type_uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

    const bool is_skinning_used = attributes.has("JOINTS_0") and attributes.has("WEIGHTS_0");

    const int required_index_space  = mesh.indices_count * (is_index_type_uint16 ? sizeof(uint16_t) : sizeof(uint32_t));
    const int required_vertex_space = position_count * (is_skinning_used ? sizeof(SkinnedVertex) : sizeof(Vertex));
    const int total_upload_buffer_size = required_index_space + required_vertex_space;
    uint8_t*  upload_buffer            = engine.dirty_stack.alloc<uint8_t>(total_upload_buffer_size);
    const int index_buffer_glb_offset =
        buffer_views.idx(index_buffer_view).integer("byteOffset") + index_accessor.integer("byteOffset");

    SDL_memset(upload_buffer, 0, static_cast<size_t>(total_upload_buffer_size));

    if (IndexType::UINT8 == index_type)
    {
      uint32_t*      dst = reinterpret_cast<uint32_t*>(upload_buffer);
      const uint8_t* src = &binary_data[index_buffer_glb_offset];

      for (unsigned i = 0; i < mesh.indices_count; ++i)
        dst[i] = src[i];
    }
    else if (IndexType::UINT16 == index_type)
    {
      uint16_t*       dst = reinterpret_cast<uint16_t*>(upload_buffer);
      const uint16_t* src = reinterpret_cast<const uint16_t*>(&binary_data[index_buffer_glb_offset]);

      for (unsigned i = 0; i < mesh.indices_count; ++i)
        dst[i] = src[i];
    }
    else
    {
      SDL_memcpy(upload_buffer, &binary_data[index_buffer_glb_offset], static_cast<size_t>(required_index_space));
    }

    const int dst_elements_begin_offset = required_index_space;
    const int dst_element_size          = is_skinning_used ? sizeof(SkinnedVertex) : sizeof(Vertex);

    {
      Seeker    buffer_view         = buffer_views.idx(position_buffer_view);
      const int view_glb_offset     = buffer_view.integer("byteOffset");
      const int accessor_glb_offset = position_accessor.integer("byteOffset");
      const int start_offset        = view_glb_offset + accessor_glb_offset;

      int dst_offset_to_position =
          static_cast<int>(is_skinning_used ? offsetof(SkinnedVertex, position) : offsetof(Vertex, position));

      int src_stride = buffer_view.integer("stride");
      src_stride     = src_stride ? src_stride : sizeof(vec3);

      for (int i = 0; i < position_count; ++i)
      {
        int          upload_buffer_offset = dst_elements_begin_offset + (dst_element_size * i) + dst_offset_to_position;
        uint8_t*     dst_raw_ptr          = &upload_buffer[upload_buffer_offset];
        float*       dst                  = reinterpret_cast<float*>(dst_raw_ptr);
        const float* src = reinterpret_cast<const float*>(&binary_data[start_offset + (src_stride * i)]);
        SDL_memcpy(dst, src, sizeof(vec3));
      }
    }

    {
      Seeker    accessor            = accessors.idx(attributes.integer("NORMAL"));
      Seeker    buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
      const int view_glb_offset     = buffer_view.integer("byteOffset");
      const int accessor_glb_offset = accessor.integer("byteOffset");
      const int start_offset        = view_glb_offset + accessor_glb_offset;

      int dst_offset_to_normal =
          static_cast<int>(is_skinning_used ? offsetof(SkinnedVertex, normal) : offsetof(Vertex, normal));

      int src_stride = buffer_view.integer("stride");
      src_stride     = src_stride ? src_stride : sizeof(vec3);

      for (int i = 0; i < position_count; ++i)
      {
        int          upload_buffer_offset = dst_elements_begin_offset + (dst_element_size * i) + dst_offset_to_normal;
        uint8_t*     dst_raw_ptr          = &upload_buffer[upload_buffer_offset];
        float*       dst                  = reinterpret_cast<float*>(dst_raw_ptr);
        const float* src = reinterpret_cast<const float*>(&binary_data[start_offset + (src_stride * i)]);
        SDL_memcpy(dst, src, sizeof(vec3));
      }
    }

    if (attributes.has("TEXCOORD_0"))
    {
      Seeker    accessor            = accessors.idx(attributes.integer("TEXCOORD_0"));
      Seeker    buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
      const int view_glb_offset     = buffer_view.integer("byteOffset");
      const int accessor_glb_offset = accessor.integer("byteOffset");
      const int start_offset        = view_glb_offset + accessor_glb_offset;

      int dst_offset_to_texcoord =
          static_cast<int>(is_skinning_used ? offsetof(SkinnedVertex, texcoord) : offsetof(Vertex, texcoord));

      int src_stride = buffer_view.integer("stride");
      src_stride     = src_stride ? src_stride : sizeof(vec2);

      for (int i = 0; i < position_count; ++i)
      {
        int          upload_buffer_offset = dst_elements_begin_offset + (dst_element_size * i) + dst_offset_to_texcoord;
        uint8_t*     dst_raw_ptr          = &upload_buffer[upload_buffer_offset];
        float*       dst                  = reinterpret_cast<float*>(dst_raw_ptr);
        const float* src = reinterpret_cast<const float*>(&binary_data[start_offset + (src_stride * i)]);
        SDL_memcpy(dst, src, sizeof(vec2));
      }
    }

    if (is_skinning_used)
    {
      {
        Seeker         accessor            = accessors.idx(attributes.integer("JOINTS_0"));
        Seeker         buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
        const int      view_glb_offset     = buffer_view.integer("byteOffset");
        const int      accessor_glb_offset = accessor.integer("byteOffset");
        const int      start_offset        = view_glb_offset + accessor_glb_offset;
        SkinnedVertex* dst_vertices        = reinterpret_cast<SkinnedVertex*>(&upload_buffer[required_index_space]);

        int src_stride = buffer_view.integer("stride");
        src_stride     = src_stride ? src_stride : (4 * sizeof(uint16_t));

        for (int i = 0; i < position_count; ++i)
        {
          uint16_t*       dst = dst_vertices[i].joint;
          const uint16_t* src = reinterpret_cast<const uint16_t*>(&binary_data[start_offset + (src_stride * i)]);
          SDL_memcpy(dst, src, 4 * sizeof(uint16_t));
        }
      }

      {
        Seeker         accessor            = accessors.idx(attributes.integer("WEIGHTS_0"));
        Seeker         buffer_view         = buffer_views.idx(accessor.integer("bufferView"));
        const int      view_glb_offset     = buffer_view.integer("byteOffset");
        const int      accessor_glb_offset = accessor.integer("byteOffset");
        const int      start_offset        = view_glb_offset + accessor_glb_offset;
        SkinnedVertex* dst_vertices        = reinterpret_cast<SkinnedVertex*>(&upload_buffer[required_index_space]);

        int src_stride = buffer_view.integer("stride");
        src_stride     = src_stride ? src_stride : sizeof(vec4);

        for (int i = 0; i < position_count; ++i)
        {
          float*       dst = dst_vertices[i].weight;
          const float* src = reinterpret_cast<const float*>(&binary_data[start_offset + (src_stride * i)]);
          SDL_memcpy(dst, src, sizeof(vec4));
        }
      }
    }

    VkDeviceSize host_buffer_offset = 0;

    {
      GpuMemoryBlock& block = engine.memory_blocks.host_visible_transfer_source;
      host_buffer_offset    = block.stack_pointer;
      block.stack_pointer += align(static_cast<VkDeviceSize>(total_upload_buffer_size), block.alignment);
    }

    {
      uint8_t* mapped_gpu_memory = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory, host_buffer_offset,
                  total_upload_buffer_size, 0, (void**)&mapped_gpu_memory);
      SDL_memcpy(mapped_gpu_memory, upload_buffer, static_cast<size_t>(total_upload_buffer_size));
      vkUnmapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory);
    }

    {
      GpuMemoryBlock& block = engine.memory_blocks.device_local;

      mesh.indices_offset = block.stack_pointer;
      block.stack_pointer += align(static_cast<VkDeviceSize>(required_index_space), block.alignment);

      mesh.vertices_offset = block.stack_pointer;
      block.stack_pointer += align(static_cast<VkDeviceSize>(required_vertex_space), block.alignment);
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = engine.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(engine.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkBufferCopy copies[] = {
          {
              .srcOffset = 0,
              .dstOffset = mesh.indices_offset,
              .size      = static_cast<VkDeviceSize>(required_index_space),
          },
          {
              .srcOffset = static_cast<VkDeviceSize>(required_index_space),
              .dstOffset = mesh.vertices_offset,
              .size      = static_cast<VkDeviceSize>(required_vertex_space),
          },
      };

      vkCmdCopyBuffer(cmd, engine.gpu_host_visible_transfer_source_memory_buffer, engine.gpu_device_local_memory_buffer,
                      SDL_arraysize(copies), copies);
    }

    {
      VkBufferMemoryBarrier barriers[] = {
          {
              .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
              .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
              .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .buffer              = engine.gpu_device_local_memory_buffer,
              .offset              = mesh.indices_offset,
              .size                = static_cast<VkDeviceSize>(required_index_space),
          },
          {
              .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
              .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
              .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .buffer              = engine.gpu_device_local_memory_buffer,
              .offset              = mesh.vertices_offset,
              .size                = static_cast<VkDeviceSize>(required_vertex_space),
          },
      };

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                           SDL_arraysize(barriers), barriers, 0, nullptr);
    }

    vkEndCommandBuffer(cmd);

    VkFence data_upload_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(engine.device, &ci, nullptr, &data_upload_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(engine.graphics_queue, 1, &submit, data_upload_fence);
    }

    vkWaitForFences(engine.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine.device, data_upload_fence, nullptr);
    vkFreeCommandBuffers(engine.device, engine.graphics_command_pool, 1, &cmd);

    engine.memory_blocks.host_visible_transfer_source.stack_pointer = 0;
  }

  // ---------------------------------------------------------------------------
  // NODES
  // ---------------------------------------------------------------------------

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    Node&  node      = scene_graph.nodes.data[node_idx];
    Seeker node_json = document.node("nodes").idx(node_idx);

    if (node_json.has("children"))
    {
      node.flags |= Node::Property::Children;
      node.children.alloc(engine.permanent_stack, node_json.node("children").elements_count());

      for (int child_idx = 0; child_idx < node.children.count; ++child_idx)
      {
        node.children.data[child_idx] = node_json.node("children").idx_integer(child_idx);
      }
    }
    else
    {
      node.children.reset();
    }

    if (node_json.has("matrix"))
    {
      node.flags |= Node::Property::Matrix;
      Seeker matrix = node_json.node("matrix");

      for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
          node.matrix[column][row] = matrix.idx_float((4 * row) + column);
    }

    if (node_json.has("rotation"))
    {
      node.flags |= Node::Property::Rotation;
      Seeker rotation = node_json.node("rotation");
      for (int i = 0; i < 4; ++i)
      {
        node.rotation[i] = rotation.idx_float(i);
      }
    }

    if (node_json.has("translation"))
    {
      node.flags |= Node::Property::Translation;
      Seeker translation = node_json.node("translation");
      for (int i = 0; i < 3; ++i)
      {
        node.translation[i] = translation.idx_float(i);
      }
    }

    if (node_json.has("scale"))
    {
      node.flags |= Node::Property::Scale;
      Seeker scale = node_json.node("scale");
      for (int i = 0; i < 3; ++i)
      {
        node.scale[i] = scale.idx_float(i);
      }
    }

    if (node_json.has("mesh"))
    {
      node.flags |= Node::Property::Mesh;
      node.mesh = node_json.integer("mesh");
    }

    if (node_json.has("skin"))
    {
      node.flags |= Node::Property::Skin;
      node.skin = node_json.integer("skin");
    }
  }

  // ---------------------------------------------------------------------------
  // SCENES
  // ---------------------------------------------------------------------------

  for (int scene_idx = 0; scene_idx < scene_graph.scenes.count; ++scene_idx)
  {
    Scene& scene      = scene_graph.scenes.data[scene_idx];
    Seeker scene_json = document.node("scenes").idx(scene_idx);
    Seeker nodes_json = scene_json.node("nodes");

    scene.nodes.alloc(engine.permanent_stack, nodes_json.elements_count());
    for (int node_idx = 0; node_idx < scene.nodes.count; ++node_idx)
    {
      scene.nodes.data[node_idx] = nodes_json.idx_integer(node_idx);
    }
  }

  // ---------------------------------------------------------------------------
  // ANIMATIONS
  // ---------------------------------------------------------------------------
  Seeker animations_json = document.node("animations");
  for (int animation_idx = 0; animation_idx < scene_graph.animations.count; ++animation_idx)
  {
    Seeker animation_json = animations_json.idx(animation_idx);
    Seeker channels_json  = animation_json.node("channels");
    Seeker samplers_json  = animation_json.node("samplers");

    int channels_count = channels_json.elements_count();
    int samplers_count = samplers_json.elements_count();

    Animation& current_animation = scene_graph.animations.data[animation_idx];

    current_animation.channels.alloc(engine.permanent_stack, channels_count);
    current_animation.samplers.alloc(engine.permanent_stack, samplers_count);

    for (int channel_idx = 0; channel_idx < channels_count; ++channel_idx)
    {
      Seeker            channel_json    = channels_json.idx(channel_idx);
      Seeker            target_json     = channel_json.node("target");
      AnimationChannel& current_channel = current_animation.channels.data[channel_idx];

      current_channel.sampler_idx     = channel_json.integer("sampler");
      current_channel.target_node_idx = target_json.integer("node");

      Seeker      path_json  = target_json.node("path");
      const char* path_value = &path_json.data[8];

      if (0 == SDL_memcmp(path_value, "rotation", 8))
      {
        current_channel.target_path = AnimationChannel::Path::Rotation;
      }
      else if (0 == SDL_memcmp(path_value, "translation", 11))
      {
        current_channel.target_path = AnimationChannel::Path::Translation;
      }
      else if (0 == SDL_memcmp(path_value, "scale", 5))
      {
        current_channel.target_path = AnimationChannel::Path::Scale;
      }
      else
      {
        SDL_assert(false);
      }
    }

    for (int sampler_idx = 0; sampler_idx < samplers_count; ++sampler_idx)
    {
      Seeker            sampler_json    = samplers_json.idx(sampler_idx);
      AnimationSampler& current_sampler = current_animation.samplers.data[sampler_idx];

      int input  = sampler_json.integer("input");
      int output = sampler_json.integer("output");

      Seeker input_accessor  = accessors.idx(input);
      Seeker output_accessor = accessors.idx(output);

      int input_elements  = input_accessor.integer("count");
      int output_elements = output_accessor.integer("count");

      const char* interpolation = &sampler_json.node("interpolation").data[17];
      if (0 == SDL_memcmp("CUBICSPLINE", interpolation, 11))
      {
        // In cubic-spline interpolation keyframe each time point maps to 3 vec3 elements
        SDL_assert(input_elements == output_elements / 3);
        current_sampler.interpolation = AnimationSampler::Interpolation::CubicSpline;
      }
      else if (0 == SDL_memcmp("LINEAR", interpolation, 6))
      {
        // In linear interpolation time maps to values 1:1 in count, so this should be always true
        SDL_assert(input_elements == output_elements);
        current_sampler.interpolation = AnimationSampler::Interpolation::Linear;
      }
      else
      {
        // I have no clue if I support it or not!
        // I'll leave a trap for potential future debugging.
        SDL_assert(false);
        current_sampler.interpolation = AnimationSampler::Interpolation::Step;
      }

      current_sampler.keyframes_count = input_elements;

      int input_buffer_view_idx  = input_accessor.integer("bufferView");
      int output_buffer_view_idx = output_accessor.integer("bufferView");

      Seeker input_buffer_view  = buffer_views.idx(input_buffer_view_idx);
      Seeker output_buffer_view = buffer_views.idx(output_buffer_view_idx);

      enum class Type : unsigned
      {
        Scalar = 1,
        Vec3   = 3,
        Vec4   = 4
      };

      Type        output_type         = Type::Scalar;
      Seeker      output_type_json    = output_accessor.node("type");
      const char* output_type_str_ptr = &output_type_json.data[8];

      if (0 == SDL_memcmp(output_type_str_ptr, "VEC3", 4))
      {
        output_type = Type::Vec3;
      }
      else if (0 == SDL_memcmp(output_type_str_ptr, "VEC4", 4))
      {
        output_type = Type::Vec4;
      }
      else if (0 == SDL_memcmp(output_type_str_ptr, "SCALAR", 6))
      {
        output_type = Type::Scalar;
      }
      else
      {
        SDL_assert(false);
      }

      current_sampler.times = engine.permanent_stack.alloc<float>(input_elements);
      current_sampler.values =
          engine.permanent_stack.alloc<float>(static_cast<unsigned>(output_type) * output_elements);

      {
        const int input_view_glb_offset     = input_buffer_view.integer("byteOffset");
        const int input_accessor_glb_offset = input_accessor.integer("byteOffset");
        const int input_start_offset        = input_view_glb_offset + input_accessor_glb_offset;

        int input_stride = input_buffer_view.integer("stride");
        input_stride     = input_stride ? input_stride : sizeof(float);

        for (int i = 0; i < input_elements; ++i)
        {
          float*       dst = &current_sampler.times[i];
          const float* src = reinterpret_cast<const float*>(&binary_data[input_start_offset + (input_stride * i)]);
          *dst             = *src;
        }
      }

      current_sampler.time_frame[0] = current_sampler.times[0];
      current_sampler.time_frame[1] = current_sampler.times[current_sampler.keyframes_count - 1];

      {
        const int output_view_glb_offset     = output_buffer_view.integer("byteOffset");
        const int output_accessor_glb_offset = output_accessor.integer("byteOffset");
        const int output_start_offset        = output_view_glb_offset + output_accessor_glb_offset;

        int output_stride = output_buffer_view.integer("stride");
        output_stride     = output_stride ? output_stride : static_cast<unsigned>(output_type) * sizeof(float);

        for (int i = 0; i < output_elements; ++i)
        {
          float*       dst = &current_sampler.values[static_cast<unsigned>(output_type) * i];
          const float* src = reinterpret_cast<const float*>(&binary_data[output_start_offset + (output_stride * i)]);

          for (unsigned j = 0; j < static_cast<unsigned>(output_type); ++j)
          {
            dst[j] = src[j];
          }
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // SKINS
  // ---------------------------------------------------------------------------
  Seeker skins_json = document.node("skins");
  for (int skin_idx = 0; skin_idx < scene_graph.skins.count; ++skin_idx)
  {
    Seeker skin_json = skins_json.idx(skin_idx);
    Skin&  skin      = scene_graph.skins[skin_idx];

    skin.skeleton = skin_json.integer("skeleton");

    Seeker joints_json = skin_json.node("joints");
    skin.joints.alloc(engine.permanent_stack, joints_json.elements_count());

    for (int i = 0; i < skin.joints.count; ++i)
    {
      skin.joints[i] = joints_json.idx_integer(i);
    }

    int    inverse_bind_matrices_accessor_idx = skin_json.integer("inverseBindMatrices");
    Seeker accessor                           = accessors.idx(inverse_bind_matrices_accessor_idx);
    skin.inverse_bind_matrices.alloc(engine.permanent_stack, accessor.integer("count"));

    Seeker buffer_view = buffer_views.idx(accessor.integer("bufferView"));

    int glb_start_offset = buffer_view.integer("byteOffset") + accessor.integer("byteOffset");
    int glb_stride       = buffer_view.integer("stride");
    glb_stride           = glb_stride ? glb_stride : sizeof(mat4x4);

    for (int i = 0; i < skin.inverse_bind_matrices.count; ++i)
    {
      const uint8_t* src = &binary_data[glb_start_offset + (glb_stride * i)];
      SDL_memcpy(skin.inverse_bind_matrices[i], src, sizeof(mat4x4));
    }
  }

  engine.dirty_stack.reset();

  uint64_t duration_ticks = SDL_GetPerformanceCounter() - start;
  float    elapsed_ms     = 1000.0f * ((float)duration_ticks / (float)SDL_GetPerformanceFrequency());
  auto     align_text     = [](float in) -> const char* { return (in > 10.0f) ? ((in > 100.0f) ? " " : "  ") : "   "; };
  SDL_Log("parsing GLB took:%s%.4f ms (%s)", align_text(elapsed_ms), elapsed_ms, path);

  return scene_graph;
}