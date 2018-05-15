#include "gltf.hh"
#include "stb_image.h"
#include "utility.hh"
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

bool is_open_bracket(char in)
{
  return ('{' == in) or ('[' == in);
}

bool is_closing_bracket(char in)
{
  return ('}' == in) or (']' == in);
}

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

  const Seeker document     = Seeker{json_data, json_chunk_length};
  const Seeker buffer_views = document.node("bufferViews");

  scene_graph.materials.count = document.node("materials").elements_count();
  scene_graph.materials.data  = engine.double_ended_stack.allocate_front<Material>(scene_graph.materials.count);

  scene_graph.meshes.count = document.node("meshes").elements_count();
  scene_graph.meshes.data  = engine.double_ended_stack.allocate_front<Mesh>(scene_graph.meshes.count);

  scene_graph.nodes.count = document.node("nodes").elements_count();
  scene_graph.nodes.data  = engine.double_ended_stack.allocate_front<Node>(scene_graph.nodes.count);

  scene_graph.scenes.count = document.node("scenes").elements_count();
  scene_graph.scenes.data  = engine.double_ended_stack.allocate_front<Scene>(scene_graph.scenes.count);

  if (document.has("animations"))
  {
    scene_graph.animations.count = document.node("animations").elements_count();
    scene_graph.animations.data  = engine.double_ended_stack.allocate_front<Animation>(scene_graph.animations.count);
  }

  if (document.has("skins"))
  {
    scene_graph.skins.count = document.node("skins").elements_count();
    scene_graph.skins.data  = engine.double_ended_stack.allocate_front<Skin>(scene_graph.animations.count);
  }

  // ---------------------------------------------------------------------------
  // MATERIALS
  // ---------------------------------------------------------------------------

  if (document.has("images"))
  {
    Seeker images = document.node("images");

    for (int material_idx = 0; material_idx < scene_graph.materials.count; ++material_idx)
    {
      Material& material                  = scene_graph.materials.data[material_idx];
      Seeker    material_json             = document.node("materials").idx(material_idx);
      Seeker    pbrMetallicRoughness      = material_json.node("pbrMetallicRoughness");
      int       albedo_image_idx          = pbrMetallicRoughness.node("baseColorTexture").integer("index");
      int       albedo_buffer_view_idx    = images.idx(albedo_image_idx).integer("bufferView");
      int       metal_roughness_image_idx = pbrMetallicRoughness.node("metallicRoughnessTexture").integer("index");
      int       metal_roughness_buffer_view_idx = images.idx(metal_roughness_image_idx).integer("bufferView");
      int       emissive_image_idx              = material_json.node("emissiveTexture").integer("index");
      int       emissive_buffer_view_idx        = images.idx(emissive_image_idx).integer("bufferView");
      int       occlusion_image_idx             = material_json.node("occlusionTexture").integer("index");
      int       occlusion_buffer_view_idx       = images.idx(occlusion_image_idx).integer("bufferView");
      int       normal_image_idx                = material_json.node("normalTexture").integer("index");
      int       normal_buffer_view_idx          = images.idx(normal_image_idx).integer("bufferView");

      material.albedo_texture_idx          = load_texture(buffer_views.idx(albedo_buffer_view_idx));
      material.metal_roughness_texture_idx = load_texture(buffer_views.idx(metal_roughness_buffer_view_idx));
      material.emissive_texture_idx        = load_texture(buffer_views.idx(emissive_buffer_view_idx));
      material.AO_texture_idx              = load_texture(buffer_views.idx(occlusion_buffer_view_idx));
      material.normal_texture_idx          = load_texture(buffer_views.idx(normal_buffer_view_idx));
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
    uint8_t*  upload_buffer            = engine.double_ended_stack.allocate_back<uint8_t>(total_upload_buffer_size);
    const int index_buffer_glb_offset =
        buffer_views.idx(index_buffer_view).integer("byteOffset") + index_accessor.integer("byteOffset");

    SDL_memset(upload_buffer, 0, static_cast<size_t>(total_upload_buffer_size));

    if (IndexType::UINT8 == index_type)
    {
      uint32_t*      dst = reinterpret_cast<uint32_t*>(upload_buffer);
      const uint8_t* src = &binary_data[index_buffer_glb_offset];

      for (int i = 0; i < mesh.indices_count; ++i)
        dst[i] = src[i];
    }
    else if (IndexType::UINT16 == index_type)
    {
      uint16_t*       dst = reinterpret_cast<uint16_t*>(upload_buffer);
      const uint16_t* src = reinterpret_cast<const uint16_t*>(&binary_data[index_buffer_glb_offset]);

      for (int i = 0; i < mesh.indices_count; ++i)
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
        utility::copy<float, 3>(dst, src);
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
        utility::copy<float, 3>(dst, src);
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
        utility::copy<float, 2>(dst, src);
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
          utility::copy<uint16_t, 4>(dst, src);
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
          utility::copy<float, 4>(dst, src);
        }
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

    mesh.indices_offset  = engine.gpu_static_geometry.allocate(static_cast<VkDeviceSize>(required_index_space));
    mesh.vertices_offset = engine.gpu_static_geometry.allocate(static_cast<VkDeviceSize>(required_vertex_space));

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
      copies[0].dstOffset = mesh.indices_offset;

      copies[1].size      = static_cast<VkDeviceSize>(required_vertex_space);
      copies[1].srcOffset = static_cast<VkDeviceSize>(required_index_space);
      copies[1].dstOffset = mesh.vertices_offset;

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
      barriers[0].offset              = mesh.indices_offset;
      barriers[0].size                = static_cast<VkDeviceSize>(required_index_space);

      barriers[1].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      barriers[1].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      barriers[1].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
      barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[1].buffer              = engine.gpu_static_geometry.buffer;
      barriers[1].offset              = mesh.vertices_offset;
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
      node.set(Node::Property::Children);
      node.children.count = node_json.node("children").elements_count();
      node.children.data  = engine.double_ended_stack.allocate_front<int>(node.children.count);

      for (int child_idx = 0; child_idx < node.children.count; ++child_idx)
      {
        node.children.data[child_idx] = node_json.node("children").idx_integer(child_idx);
      }
    }
    else
    {
      node.children.count = 0;
      node.children.data  = nullptr;
    }

    if (node_json.has("rotation"))
    {
      node.set(Node::Property::Rotation);
      Seeker rotation = node_json.node("rotation");
      for (int i = 0; i < 4; ++i)
      {
        node.rotation[i] = rotation.idx_float(i);
      }
    }

    if (node_json.has("translation"))
    {
      node.set(Node::Property::Translation);
      Seeker translation = node_json.node("translation");
      for (int i = 0; i < 3; ++i)
      {
        node.translation[i] = translation.idx_float(i);
      }
    }

    if (node_json.has("scale"))
    {
      node.set(Node::Property::Scale);
      Seeker scale = node_json.node("scale");
      for (int i = 0; i < 3; ++i)
      {
        node.scale[i] = scale.idx_float(i);
      }
    }

    if (node_json.has("mesh"))
    {
      node.set(Node::Property::Mesh);
      node.mesh = node_json.integer("mesh");
    }

    if (node_json.has("skin"))
    {
      node.set(Node::Property::Skin);
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

    scene.nodes.count = nodes_json.elements_count();
    scene.nodes.data  = engine.double_ended_stack.allocate_front<int>(scene.nodes.count);

    for (int node_idx = 0; node_idx < scene.nodes.count; ++node_idx)
    {
      scene.nodes.data[node_idx] = nodes_json.idx_integer(node_idx);
    }
  }

  // ---------------------------------------------------------------------------
  // ANIMATIONS
  // ---------------------------------------------------------------------------
  SDL_Log("%s : %d animation(s)", path, scene_graph.animations.count);

  Seeker animations_json = document.node("animations");
  for (int animation_idx = 0; animation_idx < scene_graph.animations.count; ++animation_idx)
  {
    Seeker animation_json = animations_json.idx(animation_idx);
    Seeker channels_json  = animation_json.node("channels");
    Seeker samplers_json  = animation_json.node("samplers");

    int channels_count = channels_json.elements_count();
    int samplers_count = samplers_json.elements_count();

    Animation& current_animation = scene_graph.animations.data[animation_idx];

    current_animation.channels.count = channels_count;
    current_animation.channels.data  = engine.double_ended_stack.allocate_front<AnimationChannel>(channels_count);

    current_animation.samplers.count = samplers_count;
    current_animation.samplers.data  = engine.double_ended_stack.allocate_front<AnimationSampler>(samplers_count);

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

      SDL_assert(input_elements == output_elements);

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

      current_sampler.times = engine.double_ended_stack.allocate_front<float>(input_elements);
      current_sampler.values =
          engine.double_ended_stack.allocate_front<float>(static_cast<unsigned>(output_type) * input_elements);

      SDL_Log("sampler %d", sampler_idx);

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

          SDL_Log("time %.3f", *src);

          *dst = *src;
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

          for (int j = 0; j < static_cast<unsigned>(output_type); ++j)
          {
            dst[j] = src[j];
          }

          SDL_Log("value %.3f %.3f %.3f", dst[0], dst[1], dst[2]);
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // SKINS
  // ---------------------------------------------------------------------------
  SDL_Log("%s : %d skin(s)", path, scene_graph.skins.count);

  Seeker skins_json = document.node("skins");
  for (int skin_idx = 0; skin_idx < scene_graph.skins.count; ++skin_idx)
  {
    Seeker skin_json = skins_json.idx(skin_idx);
    Skin&  skin      = scene_graph.skins[skin_idx];

    skin.skeleton = skin_json.integer("skeleton");

    Seeker joints_json = skin_json.node("joints");
    skin.joints.count  = joints_json.elements_count();
    skin.joints.data   = engine.double_ended_stack.allocate_front<int>(skin.joints.count);

    for (int i = 0; i < skin.joints.count; ++i)
    {
      skin.joints[i] = joints_json.idx_integer(i);
    }

    int    inverse_bind_matrices_accessor_idx = skin_json.integer("inverseBindMatrices");
    Seeker accessor                           = accessors.idx(inverse_bind_matrices_accessor_idx);

    skin.inverse_bind_matrices.count = accessor.integer("count");
    skin.inverse_bind_matrices.data =
        engine.double_ended_stack.allocate_front<mat4x4>(skin.inverse_bind_matrices.count);

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

  stack.reset_back();

  uint64_t duration_ticks = SDL_GetPerformanceCounter() - start;
  float    elapsed_ms     = 1000.0f * ((float)duration_ticks / (float)SDL_GetPerformanceFrequency());
  SDL_Log("parsing GLB took: %.4f ms", elapsed_ms);
}

void RenderableModel::render(Engine& engine, VkCommandBuffer cmd, MVP& mvp) const noexcept
{
  const ArrayView<int>& node_indices = scene_graph.scenes.data[0].nodes;

  for (int i = 0; i < node_indices.count; ++i)
  {
    int         node_idx = node_indices.data[i];
    const Node& node     = scene_graph.nodes.data[node_idx];

    if (node.has(Node::Property::Mesh))
    {
      Mesh& mesh = scene_graph.meshes.data[node.mesh];
      // todo: think how to integrate materials into this node hopping rendering

      vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &mesh.vertices_offset);
      vkCmdPushConstants(cmd, engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::Scene3D],
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MVP), &mvp);
      vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void RenderableModel::renderColored(Engine& engine, VkCommandBuffer cmd, mat4x4 projection, mat4x4 view,
                                    vec4 global_position, quat global_orientation, vec3 model_scale, vec3 color,
                                    Engine::SimpleRendering::Passes pass, VkDeviceSize joint_ubo_offset) noexcept
{
  vec3    node_positions[32]         = {};
  quat    node_orientations[32]      = {};
  uint8_t node_parent_hierarchy[32]  = {};
  uint8_t node_shall_be_rendered[32] = {};

  for (uint8_t i = 0; i < SDL_arraysize(node_parent_hierarchy); ++i)
  {
    node_parent_hierarchy[i] = i;
  }

  for (uint8_t node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    const ArrayView<int>& children = scene_graph.nodes[node_idx].children;
    for (uint8_t child_idx = 0; child_idx < children.count; ++child_idx)
    {
      node_parent_hierarchy[children[child_idx]] = node_idx;
    }
  }

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    quat_identity(node_orientations[node_idx]);
  }

  // initialize scene nodes to start with global transforms
  ArrayView<int>& scene_root_node_indices = scene_graph.scenes[0].nodes;
  for (int node_idx : scene_root_node_indices)
  {
    utility::copy<float, 4>(node_orientations[node_idx], global_orientation);
    utility::copy<float, 3>(node_positions[node_idx], global_position);
  }

  // propagate transformations downstream
  for (uint8_t node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    Node&   current    = scene_graph.nodes[node_idx];
    uint8_t parent_idx = node_parent_hierarchy[node_idx];

    float* current_orientation = node_orientations[node_idx];
    float* current_position    = node_positions[node_idx];
    float* parent_orientation  = node_orientations[parent_idx];
    float* parent_position     = node_positions[parent_idx];

    {
      quat tmp = {};
      quat_mul(tmp, parent_orientation, current_orientation);
      utility::copy<float, 4>(current_orientation, tmp);
    }
    utility::copy<float, 3>(current_position, parent_position);

    if (animation_properties[node_idx] & Node::Property::Rotation)
    {
      quat final_animated_orientation = {};
      quat_mul(final_animated_orientation, current_orientation, animation_rotations[node_idx]);
      utility::copy<float, 4>(current_orientation, final_animated_orientation);
    }
    else if (current.has(Node::Property::Rotation))
    {
      quat final_orientation = {};
      quat_mul(final_orientation, current_orientation, current.rotation);
      utility::copy<float, 4>(current_orientation, final_orientation);
    }

    if (animation_properties[node_idx] & Node::Property::Translation)
    {
      vec3 final_animated_position = {};
      quat_mul_vec3(final_animated_position, current_orientation, animation_translations[node_idx]);
      vec3_add(current_position, current_position, final_animated_position);
    }
    else if (current.has(Node::Property::Translation))
    {
      vec3 final_position = {};
      quat_mul_vec3(final_position, current_orientation, current.translation);
      vec3_add(current_position, current_position, final_position);
    }
  }

  for (int i = 0; i < scene_root_node_indices.count; ++i)
  {
    node_shall_be_rendered[scene_root_node_indices.data[i]] = SDL_TRUE;
  }

  for (int i = 0; i < SDL_arraysize(node_shall_be_rendered); ++i)
  {
    uint8_t is_rendered        = node_shall_be_rendered[i];
    uint8_t parent             = node_parent_hierarchy[i];
    uint8_t is_parent_rendered = node_shall_be_rendered[parent];

    if ((SDL_FALSE == is_rendered) and (SDL_TRUE == is_parent_rendered))
    {
      node_shall_be_rendered[i] = SDL_TRUE;
    }
  }

  if (Engine::SimpleRendering::Passes::ColoredGeometrySkinned == pass)
  {
    Skin& skin = scene_graph.skins[0];

    mat4x4 global_transform = {};
    mat4x4_identity(global_transform);
    mat4x4_translate_in_place(global_transform, global_position[0], global_position[1], global_position[2]);

    mat4x4 rotation = {};
    mat4x4_from_quat(rotation, global_orientation);
    mat4x4_mul(global_transform, global_transform, rotation);

    mat4x4 inverted_global_transform = {};
    mat4x4_invert(inverted_global_transform, global_transform);

    //
    // BRUTE-FORCE FOR NOW!!
    // todo: replace this with a good quality code when the feature works
    //
    mat4x4 local_transform_matrices[32] = {};

    for (int i = 0; i < 32; ++i)
    {
      mat4x4 rotation = {};
      mat4x4_from_quat(rotation, node_orientations[i]);

      float* position_src = node_positions[i];

      mat4x4 model = {};
      mat4x4_identity(model);
      mat4x4_translate(model, position_src[0], position_src[1], position_src[2]);
      mat4x4_mul(model, model, rotation);
      //mat4x4_scale_aniso(model, model, model_scale[0], model_scale[1], model_scale[2]);

      mat4x4_dup(local_transform_matrices[i], model);
    }

    uint8_t* gpu_joint_matrices = nullptr;
    vkMapMemory(engine.generic_handles.device, engine.ubo_host_visible.memory, joint_ubo_offset, 12 * sizeof(mat4x4), 0,
                (void**)(&gpu_joint_matrices));

    SDL_assert(skin.joints.count <= 12);

    for (int joint_id = 0; joint_id < skin.joints.count; ++joint_id)
    {
      int joint_node_id = skin.joints[joint_id];

      mat4x4 tmp = {};
      mat4x4_mul(tmp, local_transform_matrices[joint_node_id], inverted_global_transform);

      mat4x4 joint_matrix = {};
      mat4x4_mul(joint_matrix, skin.inverse_bind_matrices[joint_id], tmp);

      SDL_memcpy(&gpu_joint_matrices[sizeof(mat4x4) * joint_id], joint_matrix, sizeof(mat4x4));
    }

    vkUnmapMemory(engine.generic_handles.device, engine.ubo_host_visible.memory);
  }

  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, projection, view);

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (node_shall_be_rendered[node_idx] and scene_graph.nodes.data[node_idx].has(Node::Property::Mesh))
    {
      mat4x4 rotation = {};
      mat4x4_from_quat(rotation, node_orientations[node_idx]);

      float* position_src = node_positions[node_idx];

      mat4x4 model = {};
      mat4x4_identity(model);
      mat4x4_translate(model, position_src[0], position_src[1], position_src[2]);
      mat4x4_mul(model, model, rotation);
      mat4x4_scale_aniso(model, model, model_scale[0], model_scale[1], model_scale[2]);

      int         mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &mesh.vertices_offset);

      mat4x4 calculated_mvp = {};
      mat4x4_mul(calculated_mvp, projection_view, model);

      vkCmdPushConstants(cmd, engine.simple_rendering.pipeline_layouts[pass], VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(mat4x4), calculated_mvp);

      vkCmdPushConstants(cmd, engine.simple_rendering.pipeline_layouts[pass], VK_SHADER_STAGE_FRAGMENT_BIT,
                         sizeof(mat4x4), sizeof(vec3), color);

      vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }

  engine.double_ended_stack.reset_back();
}

void RenderableModel::renderRaw(Engine& engine, VkCommandBuffer cmd) const noexcept
{
  // todo: this needs proper multinode implementation, but since I only use it for drawing boxes and a single model,
  // then I'll fit the function to render only it correctly. In the future when the full implementation is needed
  // (just like in renderColored) I'll need to fix it... or maybe not? who knows?

  const Node& node = scene_graph.nodes.data[1];
  Mesh&       mesh = scene_graph.meshes.data[node.mesh];
  vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, mesh.indices_offset, mesh.indices_type);
  vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &mesh.vertices_offset);
  vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
}

} // namespace gltf
