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

size_t find_substring_idx(const char* big_string, const size_t big_string_length, const char* small_string)
{
  size_t result              = 0;
  size_t small_string_length = SDL_strlen(small_string);

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
    Seeker      element  = idx(desired_array_element);
    const char* adjusted = &element.data[1];
    double      result   = SDL_strtod(adjusted, nullptr);
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

  // ---------------------------------------------------------------------------
  // MATERIALS
  // ---------------------------------------------------------------------------

  if (document.has("images"))
  {
    Seeker images       = document.node("images");
    int    images_count = document.node("images").elements_count();

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

    const bool is_index_type_uint16 = (IndexType::UINT16 == index_type);

    mesh.indices_count = static_cast<uint32_t>(index_accessor.integer("count"));
    mesh.indices_type  = is_index_type_uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

    const int required_index_space  = mesh.indices_count * (is_index_type_uint16 ? sizeof(uint16_t) : sizeof(uint32_t));
    const int required_vertex_space = position_count * sizeof(Vertex);
    const int total_upload_buffer_size = required_index_space + required_vertex_space;
    uint8_t*  upload_buffer            = engine.double_ended_stack.allocate_back<uint8_t>(total_upload_buffer_size);
    const int index_buffer_glb_offset  = buffer_views.idx(index_buffer_view).integer("byteOffset") + index_accessor.integer("byteOffset");

    SDL_memset(upload_buffer, 0, static_cast<size_t>(total_upload_buffer_size));

    if (IndexType::UINT8 == index_type)
    {
      uint32_t*      dst = reinterpret_cast<uint32_t*>(upload_buffer);
      const uint8_t* src = &binary_data[index_buffer_glb_offset];

      for (int i = 0; i < mesh.indices_count; ++i)
        dst[i]   = src[i];
    }
    else if (IndexType::UINT16 == index_type)
    {
      uint16_t*       dst = reinterpret_cast<uint16_t*>(upload_buffer);
      const uint16_t* src = reinterpret_cast<const uint16_t*>(&binary_data[index_buffer_glb_offset]);

      for (int i = 0; i < mesh.indices_count; ++i)
        dst[i]   = src[i];
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
          dst[j]   = src[j];
      }
    }

    {
      Seeker    accessor            = accessors.idx(attributes.integer("NORMAL"));
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
          dst[j]   = src[j];
      }
    }

    if (attributes.has("TEXCOORD_0"))
    {
      Seeker    accessor            = accessors.idx(attributes.integer("TEXCOORD_0"));
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
          dst[j]   = src[j];
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
      node.set(Node::ChildrenBit);
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
        node.children.data = nullptr;
    }

    if (node_json.has("rotation"))
    {
      node.set(Node::RotationBit);
      Seeker rotation = node_json.node("rotation");
      for (int i = 0; i < 4; ++i)
      {
        node.rotation[i] = rotation.idx_float(i);
      }
    }

    if (node_json.has("mesh"))
    {
      node.set(Node::MeshBit);
      node.mesh = node_json.integer("mesh");
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

    if (node.has(Node::MeshBit))
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
                                    vec4 global_position, quat global_orientation, vec3 model_scale, vec3 color) const
    noexcept
{
  // Compute hierarchy
  uint8_t node_parent_hierarchy[32] = {};

  // pointing at self as a parent means that the node has no parent (or the scene is it's parent)
  for (uint8_t i = 0; i < SDL_arraysize(node_parent_hierarchy); ++i)
  {
    node_parent_hierarchy[i] = i;
  }

  for (uint8_t node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    for (int child_idx = 0; child_idx < scene_graph.nodes.data[node_idx].children.count; ++child_idx)
    {
      node_parent_hierarchy[scene_graph.nodes.data[node_idx].children.data[child_idx]] = node_idx;
    }
  }

  struct NodeTransforms
  {
    vec4 position;
    quat orientation;
  } node_transforms[32] = {};

  for(NodeTransforms& transform : node_transforms)
  {
      quat_identity(transform.orientation);
  }

  // initialize scene nodes to start with global transforms
  ArrayView<int>& scene_root_node_indices = scene_graph.scenes.data[0].nodes;
  for (int sceen_node_idx = 0; sceen_node_idx < scene_root_node_indices.count; ++sceen_node_idx)
  {
    int node_idx = scene_root_node_indices.data[sceen_node_idx];

    for (int i = 0; i < 4; ++i)
    {
      node_transforms[node_idx].orientation[i] = global_orientation[i];
    }

    for (int i = 0; i < 4; ++i)
    {
      node_transforms[node_idx].position[i] = global_position[i];
    }
  }

  // propagate transformations downstream
  for (uint8_t node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    Node&   current    = scene_graph.nodes.data[node_idx];
    uint8_t parent_idx = node_parent_hierarchy[node_idx];

    NodeTransforms& current_transform = node_transforms[node_idx];
    NodeTransforms& parent_transform  = node_transforms[parent_idx];

    for (int i = 0; i < 4; ++i)
    {
      current_transform.orientation[i] = parent_transform.orientation[i];
    }

    for (int i = 0; i < 4; ++i)
    {
      current_transform.position[i] = parent_transform.position[i];
    }

    // @todo: I can probably get rid of this if the rotation quaternion inside of node is zeroed
    // (and I bet it is already. Requires tinkering anyway.
    if (current.has(Node::RotationBit))
    {
      quat result = {};
      quat_mul(result, current_transform.orientation, current.rotation);

      for (int i = 0; i < 4; ++i)
      {
        current_transform.position[i] = result[i];
      }
    }
  }

  uint8_t node_shall_be_rendered[32] = {};

  for (int i = 0; i < scene_root_node_indices.count; ++i)
  {
    node_shall_be_rendered[scene_root_node_indices.data[i]] = 1;
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

  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, projection, view);

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (node_shall_be_rendered[node_idx] and scene_graph.nodes.data[node_idx].has(Node::MeshBit))
    {
      mat4x4 rotation = {};
      mat4x4_from_quat(rotation, node_transforms[node_idx].orientation);

      float* position_src = node_transforms[node_idx].position;

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

      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), calculated_mvp);

      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

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
