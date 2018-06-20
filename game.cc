#include "game.hh"
#include "cubemap.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_timer.h>

#define VR_LEVEL_SCALE 25.0f

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

float clamp(float val, float min, float max)
{
  return (val < min) ? min : (val > max) ? max : val;
}

int find_first_higher(float* times, float current)
{
  int iter = 0;
  while (current > times[iter])
    iter += 1;
  return iter;
}

void animate_model(gltf::RenderableModel& model, float current_time_sec)
{
  if (not model.animation_enabled)
    return;

  const Animation& animation      = model.scene_graph.animations.data[0];
  const float      animation_time = current_time_sec - model.animation_start_time;

  bool is_animation_still_ongoing = false;
  for (const AnimationChannel& channel : animation.channels)
  {
    const AnimationSampler& sampler = animation.samplers[channel.sampler_idx];
    if (sampler.time_frame[1] > animation_time)
    {
      is_animation_still_ongoing = true;
      break;
    }
  }

  if (not is_animation_still_ongoing)
  {
    model.animation_enabled = false;
    return;
  }

  for (const AnimationChannel& channel : animation.channels)
  {
    const AnimationSampler& sampler = animation.samplers[channel.sampler_idx];
    if ((sampler.time_frame[1] > animation_time) and (sampler.time_frame[0] < animation_time))
    {
      int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
      int   keyframe_lower         = keyframe_upper - 1;
      float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
      float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

      if (AnimationChannel::Path::Rotation == channel.target_path)
      {
        float* a = &sampler.values[4 * keyframe_lower];
        float* b = &sampler.values[4 * keyframe_upper];
        float* c = model.animation_rotations[channel.target_node_idx];

        // quaternion lerp
        float reminder_time = 1.0f - keyframe_uniform_time;
        for (int i = 0; i < 4; ++i)
        {
          c[i] = reminder_time * a[i] + keyframe_uniform_time * b[i];
        }
        vec4_norm(c, c);

        model.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        float* a = &sampler.values[3 * keyframe_lower];
        float* b = &sampler.values[3 * keyframe_upper];
        float* c = model.animation_translations[channel.target_node_idx];

        // lerp
        for (int i = 0; i < 3; ++i)
        {
          float difference = b[i] - a[i];
          float progressed = difference * keyframe_uniform_time;
          c[i]             = a[i] + progressed;
        }

        model.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
      }
    }
  }
}

void vec3_set(float* vec, float x, float y, float z)
{
  vec[0] = x;
  vec[1] = y;
  vec[2] = z;
}

class PushBuffer
{
public:
  PushBuffer(float* container, int capacity)
      : container(container)
      , capacity(capacity)
  {
  }

  void push(float value)
  {
    for (int i = 0; i < (capacity - 1); ++i)
      container[i] = container[i + 1];
    container[capacity - 1] = value;
  }

private:
  float*    container;
  const int capacity;
};

class FunctionTimer
{
public:
  explicit FunctionTimer(PushBuffer push_buffer)
      : start_ticks(SDL_GetPerformanceCounter())
      , storage(push_buffer)
  {
  }

  FunctionTimer(float* container, int capacity)
      : FunctionTimer(PushBuffer(container, capacity))
  {
  }

  ~FunctionTimer()
  {
    uint64_t end_function_ticks = SDL_GetPerformanceCounter();
    uint64_t ticks_elapsed      = end_function_ticks - start_ticks;
    float    duration           = (float)ticks_elapsed / (float)SDL_GetPerformanceFrequency();
    storage.push(duration);
  }

private:
  const uint64_t start_ticks;
  PushBuffer     storage;
};

class CommandBufferSelector
{
public:
  CommandBufferSelector(Engine::SimpleRendering& renderer, int image_index)
      : collection(renderer.secondary_command_buffers)
      , image_index(image_index)
  {
  }

  VkCommandBuffer select(int pipeline) const
  {
    return collection[Engine::SimpleRendering::Pipeline::Count * image_index + pipeline];
  }

private:
  VkCommandBuffer* collection;
  const int        image_index;
};

class ScopedCommand
{
public:
  explicit ScopedCommand(VkCommandBuffer cmd)
      : cmd(cmd)
  {
  }

  ~ScopedCommand()
  {
    vkEndCommandBuffer(cmd);
  }

private:
  VkCommandBuffer cmd;
};

class CommandBufferStarter
{
public:
  CommandBufferStarter(VkRenderPass render_pass, VkFramebuffer framebuffer)
      : render_pass(render_pass)
      , framebuffer(framebuffer)
  {
  }

  ScopedCommand begin(VkCommandBuffer cmd, uint32_t subpass)
  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass           = render_pass,
        .subpass              = subpass,
        .framebuffer          = framebuffer,
        .occlusionQueryEnable = VK_FALSE,
    };

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(cmd, &begin);
    return ScopedCommand(cmd);
  }

private:
  VkRenderPass  render_pass;
  VkFramebuffer framebuffer;
};

class Quaternion
{
public:
  Quaternion()
      : orientation{0.0f, 0.0f, 0.0f, 1.0f}
  {
  }

  Quaternion& rotateX(float rads)
  {
    vec3 axis = {1.0, 0.0, 0.0};
    rotate(axis, rads);
    return *this;
  }

  Quaternion& rotateY(float rads)
  {
    vec3 axis = {0.0, 1.0, 0.0};
    rotate(axis, rads);
    return *this;
  }

  Quaternion& rotateZ(float rads)
  {
    vec3 axis = {0.0, 0.0, 1.0};
    rotate(axis, rads);
    return *this;
  }

  Quaternion operator*(Quaternion& rhs)
  {
    Quaternion result;
    quat_mul(result.orientation, orientation, rhs.orientation);
    return result;
  }

  float* data()
  {
    return orientation;
  }

private:
  void rotate(vec3 axis, float rads)
  {
    quat_rotate(orientation, rads, axis);
  }

  quat orientation;
};

float avg(const float* values, int n)
{
  float sum = 0.0f;
  for (int i = 0; i < n; ++i)
    sum += values[i];
  sum /= n;
  return sum;
}

class ScopedMemoryMap
{
public:
  ScopedMemoryMap(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size)
      : data(nullptr)
      , device(device)
      , memory(memory)
  {
    vkMapMemory(device, memory, offset, size, 0, &data);
  }

  ~ScopedMemoryMap()
  {
    vkUnmapMemory(device, memory);
  }

  template <typename T> T* get()
  {
    return reinterpret_cast<T*>(data);
  }

private:
  void*          data;
  VkDevice       device;
  VkDeviceMemory memory;
};

bool is_any(const bool* array, int n)
{
  for (int i = 0; i < n; ++i)
    if (array[i])
      return true;
  return false;
}

void restart_animation(gltf::RenderableModel& model, float current_time_sec)
{
  model.animation_enabled    = true;
  model.animation_start_time = current_time_sec;

  for (quat& rotation : model.animation_rotations)
    quat_identity(rotation);

  for (vec3& translation : model.animation_translations)
    for (int i = 0; i < 4; ++i)
      translation[i] = 0.0f;
}

struct VrLevelLoadResult
{
  float entrance_point[2];
  float target_goal[2];

  VkDeviceSize vertex_target_offset;
  VkDeviceSize index_target_offset;
  int          index_count;
  VkIndexType  index_type;
};

VrLevelLoadResult level_generator_vr(Engine* engine)
{
  struct Rectangle
  {
    vec2 size;
    vec2 position;
  };

  struct Building
  {
    vec2  size;
    vec2  position;
    float height;
  };

  // @todo: add reading saved data from file
  Rectangle rooms[] = {
      {
          .size     = {0.4f, 1.0f},
          .position = {0.0f, 0.5f},
      },
  };

  float    building_dim  = 0.1f;
  Building buildings[20] = {};

  for (int i = 0; i < 10; ++i)
  {
    vec2 size = {building_dim, building_dim};
    SDL_memcpy(buildings[i].size, size, sizeof(vec2));
    SDL_memcpy(buildings[i + 10].size, size, sizeof(vec2));

    vec2 left  = {0.2f + (building_dim / 2.0f), i * building_dim + (building_dim / 2.0f)};
    vec2 right = {-0.2f - (building_dim / 2.0f), i * building_dim + (building_dim / 2.0f)};

    SDL_memcpy(buildings[i].position, left, sizeof(vec2));
    SDL_memcpy(buildings[i + 10].position, right, sizeof(vec2));

    float height             = 0.08f;
    buildings[i].height      = (SDL_sinf(i * 0.5f) * height) + height;
    buildings[i + 10].height = (SDL_cosf(i * 0.5f) * height) + height;
  }

  int vertex_count = (SDL_arraysize(rooms) * 4) +     // rooms
                     (SDL_arraysize(buildings) * 20); // buildings

  int index_count = (SDL_arraysize(rooms) * 6) +     // rooms
                    (SDL_arraysize(buildings) * 30); // buildings

  using IndexType = uint16_t;

  struct Vertex
  {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
  };

  VkDeviceSize host_vertex_offset = engine->gpu_static_transfer.allocate(vertex_count * sizeof(Vertex));
  VkDeviceSize host_index_offset  = engine->gpu_static_transfer.allocate(index_count * sizeof(IndexType));

  {
    ScopedMemoryMap memory_map(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_vertex_offset,
                               vertex_count * sizeof(Vertex));

    Vertex* dst_vertices = memory_map.get<Vertex>();
    Vertex* current      = dst_vertices;

    for (const Rectangle& r : rooms)
    {
      float left   = r.position[0] - (r.size[0] / 2.0f);
      float right  = r.position[0] + (r.size[0] / 2.0f);
      float top    = r.position[1] + (r.size[1] / 2.0f);
      float bottom = r.position[1] - (r.size[1] / 2.0f);

      vec3 positions[] = {
          {left, 0.0f, bottom},
          {right, 0.0f, bottom},
          {right, 0.0f, top},
          {left, 0.0f, top},
      };

      for (unsigned i = 0; i < SDL_arraysize(positions); ++i)
      {
        SDL_memcpy(current[i].position, positions[i], sizeof(vec3));
        SDL_memset(current[i].normal, 0, sizeof(vec3));
        SDL_memset(current[i].texcoord, 0, sizeof(vec3));
      }

      current = &current[SDL_arraysize(positions)];
    }

    for (const Building& b : buildings)
    {
      float left   = b.position[0] - (b.size[0] / 2.0f);
      float right  = b.position[0] + (b.size[0] / 2.0f);
      float top    = b.position[1] + (b.size[1] / 2.0f);
      float bottom = b.position[1] - (b.size[1] / 2.0f);
      float height = -b.height;

      vec3 vertices[] = {
          // rooftop
          {left, height, bottom},
          {right, height, bottom},
          {right, height, top},
          {left, height, top},
          // front wall
          {left, 0.0f, bottom},
          {right, 0.0f, bottom},
          {right, height, bottom},
          {left, height, bottom},
          // right wall
          {right, 0.0f, bottom},
          {right, 0.0f, top},
          {right, height, top},
          {right, height, bottom},
          // left wall
          {left, 0.0f, top},
          {left, 0.0f, bottom},
          {left, height, bottom},
          {left, height, top},
          // back wall
          {right, 0.0f, top},
          {left, 0.0f, top},
          {left, height, top},
          {right, height, top},
      };

      for (unsigned i = 0; i < SDL_arraysize(vertices); ++i)
      {
        SDL_memcpy(current[i].position, vertices[i], sizeof(vec3));
        SDL_memset(current[i].normal, 0, sizeof(vec3));
        SDL_memset(current[i].texcoord, 0, sizeof(vec3));
      }

      current = &current[SDL_arraysize(vertices)];
    }
  }

  {
    ScopedMemoryMap memory_map(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_index_offset,
                               index_count * sizeof(IndexType));
    uint16_t*       dst_indices = memory_map.get<uint16_t>();

    for (uint16_t i = 0; i < SDL_arraysize(rooms); ++i)
    {
      uint16_t     rectangle_indices[] = {0, 1, 2, 2, 3, 0};
      const size_t stride              = SDL_arraysize(rectangle_indices);

      for (uint16_t& idx : rectangle_indices)
        idx += 4 * i;

      uint16_t* dst_rectangle_indices = &dst_indices[stride * i];
      SDL_memcpy(dst_rectangle_indices, rectangle_indices, sizeof(uint16_t) * stride);
    }

    for (uint16_t i = 0; i < SDL_arraysize(buildings); ++i)
    {
      const uint16_t rectangle_indices[]                       = {0, 1, 2, 2, 3, 0};
      const size_t   total_indices_per_building                = 5 * SDL_arraysize(rectangle_indices);
      uint16_t       total_indices[total_indices_per_building] = {};

      for (unsigned j = 0; j < SDL_arraysize(total_indices); ++j)
      {
        const uint16_t rectangle_index   = rectangle_indices[j % SDL_arraysize(rectangle_indices)];
        const uint16_t index_offset      = 4 * (j / SDL_arraysize(rectangle_indices));
        const uint16_t building_offset   = i * uint16_t(20);
        const uint16_t offset_from_rooms = 4 * SDL_arraysize(rooms);
        total_indices[j]                 = rectangle_index + index_offset + building_offset + offset_from_rooms;
      }

      uint16_t* dst_building_indices = &dst_indices[(SDL_arraysize(rooms) * 6) + (SDL_arraysize(total_indices) * i)];
      SDL_memcpy(dst_building_indices, total_indices, sizeof(uint16_t) * SDL_arraysize(total_indices));
    }
  }

  VkDeviceSize device_vertex_offset = engine->gpu_static_geometry.allocate(vertex_count * sizeof(Vertex));
  VkDeviceSize device_index_offset  = engine->gpu_static_geometry.allocate(index_count * sizeof(IndexType));

  VrLevelLoadResult result = {
      .entrance_point       = {0.0f, 0.0f},
      .target_goal          = {0.0f, 0.2f},
      .vertex_target_offset = device_vertex_offset,
      .index_target_offset  = device_index_offset,
      .index_count          = index_count,
      .index_type           = VK_INDEX_TYPE_UINT16,
  };

  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = engine->generic_handles.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(engine->generic_handles.device, &allocate, &cmd);

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(cmd, &begin);

    VkBufferCopy copies[] = {
        {
            .srcOffset = host_vertex_offset,
            .dstOffset = device_vertex_offset,
            .size      = vertex_count * sizeof(Vertex),
        },
        {
            .srcOffset = host_index_offset,
            .dstOffset = device_index_offset,
            .size      = index_count * sizeof(IndexType),
        },
    };

    vkCmdCopyBuffer(cmd, engine->gpu_static_transfer.buffer, engine->gpu_static_geometry.buffer, SDL_arraysize(copies),
                    copies);

    VkBufferMemoryBarrier barriers[] = {
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = engine->gpu_static_geometry.buffer,
            .offset              = device_vertex_offset,
            .size                = vertex_count * sizeof(Vertex),
        },
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = engine->gpu_static_geometry.buffer,
            .offset              = device_index_offset,
            .size                = index_count * sizeof(IndexType),
        },
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                         SDL_arraysize(barriers), barriers, 0, nullptr);
    vkEndCommandBuffer(cmd);

    VkFence data_upload_fence = VK_NULL_HANDLE;

    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(engine->generic_handles.device, &ci, nullptr, &data_upload_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(engine->generic_handles.graphics_queue, 1, &submit, data_upload_fence);
    }

    vkWaitForFences(engine->generic_handles.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine->generic_handles.device, data_upload_fence, nullptr);
    vkFreeCommandBuffers(engine->generic_handles.device, engine->generic_handles.graphics_command_pool, 1, &cmd);
  }

  engine->gpu_static_transfer.used_memory = 0;
  engine->double_ended_stack.reset_back();
  return result;
}

void update_ubo(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, VkDeviceSize offset, void* src)
{
  ScopedMemoryMap memory_map(device, memory, offset, size);
  SDL_memcpy(memory_map.get<void>(), src, size);
}

} // namespace

void Game::startup(Engine& engine)
{
  {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    unsigned char* guifont_pixels = nullptr;
    int            guifont_w      = 0;
    int            guifont_h      = 0;
    io.Fonts->GetTexDataAsRGBA32(&guifont_pixels, &guifont_w, &guifont_h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(guifont_pixels, guifont_w, guifont_h, 32, 4 * guifont_w,
                                                              SDL_PIXELFORMAT_RGBA8888);
    debug_gui.font_texture_idx = engine.load_texture(surface);
    SDL_FreeSurface(surface);

    struct Mapping
    {
      ImGuiKey_    imgui;
      SDL_Scancode sdl;
    } mappings[] = {
        // --------------------------------------------------------------------
        {ImGuiKey_Tab, SDL_SCANCODE_TAB},
        {ImGuiKey_LeftArrow, SDL_SCANCODE_LEFT},
        {ImGuiKey_RightArrow, SDL_SCANCODE_RIGHT},
        {ImGuiKey_UpArrow, SDL_SCANCODE_UP},
        {ImGuiKey_DownArrow, SDL_SCANCODE_DOWN},
        {ImGuiKey_PageUp, SDL_SCANCODE_PAGEUP},
        {ImGuiKey_PageDown, SDL_SCANCODE_PAGEDOWN},
        {ImGuiKey_Home, SDL_SCANCODE_HOME},
        {ImGuiKey_End, SDL_SCANCODE_END},
        {ImGuiKey_Insert, SDL_SCANCODE_INSERT},
        {ImGuiKey_Delete, SDL_SCANCODE_DELETE},
        {ImGuiKey_Backspace, SDL_SCANCODE_BACKSPACE},
        {ImGuiKey_Space, SDL_SCANCODE_SPACE},
        {ImGuiKey_Enter, SDL_SCANCODE_RETURN},
        {ImGuiKey_Escape, SDL_SCANCODE_ESCAPE},
        {ImGuiKey_A, SDL_SCANCODE_A},
        {ImGuiKey_C, SDL_SCANCODE_C},
        {ImGuiKey_V, SDL_SCANCODE_V},
        {ImGuiKey_X, SDL_SCANCODE_X},
        {ImGuiKey_Y, SDL_SCANCODE_Y},
        {ImGuiKey_Z, SDL_SCANCODE_Z}
        // --------------------------------------------------------------------
    };

    for (Mapping mapping : mappings)
      io.KeyMap[mapping.imgui] = mapping.sdl;

    io.RenderDrawListsFn  = nullptr;
    io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
    io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
    io.ClipboardUserData  = nullptr;

    struct CursorMapping
    {
      ImGuiMouseCursor_ imgui;
      SDL_SystemCursor  sdl;
    } cursor_mappings[] = {
        // --------------------------------------------------------------------
        {ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW},
        {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM},
        {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL},
        {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS},
        {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE},
        {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW},
        {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE}
        // --------------------------------------------------------------------
    };

    for (CursorMapping mapping : cursor_mappings)
      debug_gui.mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
  }

  {
    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      debug_gui.vertex_buffer_offsets[i] = engine.gpu_host_visible.allocate(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES);
      debug_gui.index_buffer_offsets[i]  = engine.gpu_host_visible.allocate(DebugGui::INDEX_BUFFER_CAPACITY_BYTES);
    }
  }

  // Proof of concept GLB loader
  helmet.loadGLB(engine, "../assets/DamagedHelmet.glb");
  box.loadGLB(engine, "../assets/Box.glb");
  animatedBox.loadGLB(engine, "../assets/BoxAnimated.glb");
  riggedSimple.loadGLB(engine, "../assets/RiggedSimple.glb");
  monster.loadGLB(engine, "../assets/Monster.glb");
  robot.loadGLB(engine, "../assets/robot.glb");

  {
    int cubemap_size[2]     = {512, 512};
    environment_cubemap_idx = generate_cubemap(&engine, this, "../assets/old_industrial_hall.jpg", cubemap_size);
    irradiance_cubemap_idx  = generate_irradiance_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    prefiltered_cubemap_idx = generate_prefiltered_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    brdf_lookup_idx         = generate_brdf_lookup(&engine, cubemap_size[0]);
  }

  const VkDeviceSize light_sources_ubo_size     = sizeof(LightSources);
  const VkDeviceSize skinning_matrices_ubo_size = 64 * sizeof(mat4x4);

  for (VkDeviceSize& offset : pbr_dynamic_lights_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(light_sources_ubo_size);

  for (VkDeviceSize& offset : rig_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(skinning_matrices_ubo_size);

  for (VkDeviceSize& offset : fig_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(skinning_matrices_ubo_size);

  for (VkDeviceSize& offset : monster_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(skinning_matrices_ubo_size);

  // ----------------------------------------------------------------------------------------------
  // PBR Metallic workflow material descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.pbr_metallic_workflow_material_descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &helmet_pbr_material_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &robot_pbr_material_dset);
  }

  {
    auto fill_infos = [](const Material& material, VkImageView* views, VkDescriptorImageInfo infos[5]) {
      infos[0].imageView = views[material.albedo_texture_idx];
      infos[1].imageView = views[material.metal_roughness_texture_idx];
      infos[2].imageView = views[material.emissive_texture_idx];
      infos[3].imageView = views[material.AO_texture_idx];
      infos[4].imageView = views[material.normal_texture_idx];
    };

    VkDescriptorImageInfo images[5] = {};
    for (VkDescriptorImageInfo& image : images)
    {
      image.sampler     = engine.generic_handles.texture_sampler;
      image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet update = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = SDL_arraysize(images),
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = images,
    };

    fill_infos(helmet.scene_graph.materials[0], engine.images.image_views, images);
    update.dstSet = helmet_pbr_material_dset,
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &update, 0, nullptr);

    fill_infos(robot.scene_graph.materials[0], engine.images.image_views, images);
    update.dstSet = robot_pbr_material_dset,
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &update, 0, nullptr);
  }

  // ----------------------------------------------------------------------------------------------
  // PBR IBL cubemaps and BRDF lookup table descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &pbr_ibl_environment_dset);
  }

  {
    VkDescriptorImageInfo cubemap_images[] = {
        {
            .sampler     = engine.generic_handles.texture_sampler,
            .imageView   = engine.images.image_views[irradiance_cubemap_idx],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        {
            .sampler     = engine.generic_handles.texture_sampler,
            .imageView   = engine.images.image_views[prefiltered_cubemap_idx],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
    };

    VkDescriptorImageInfo brdf_lut_image = {
        .sampler     = engine.generic_handles.texture_sampler,
        .imageView   = engine.images.image_views[brdf_lookup_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet writes[] = {
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = pbr_ibl_environment_dset,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = SDL_arraysize(cubemap_images),
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = cubemap_images,
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = pbr_ibl_environment_dset,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &brdf_lut_image,
        },
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, SDL_arraysize(writes), writes, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // PBR dynamic light sources descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.pbr_dynamic_lights_descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &pbr_dynamic_lights_dset);
  }

  {
    VkDescriptorBufferInfo ubo = {
        .buffer = engine.ubo_host_visible.buffer,
        .offset = 0, // those will be provided at command buffer recording time
        .range  = light_sources_ubo_size,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = pbr_dynamic_lights_dset,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pBufferInfo     = &ubo,
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Single texture in fragment shader descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.single_texture_in_frag_descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &skybox_cubemap_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &imgui_font_atlas_dset);
  }

  {
    VkDescriptorImageInfo image = {
        .sampler     = engine.generic_handles.texture_sampler,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image,
    };

    image.imageView = engine.images.image_views[debug_gui.font_texture_idx];
    write.dstSet    = imgui_font_atlas_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);

    image.imageView = engine.images.image_views[environment_cubemap_idx];
    write.dstSet    = skybox_cubemap_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Skinning matrices in vertex shader descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.skinning_matrices_descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &monster_skinning_matrices_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &rig_skinning_matrices_dset);
  }

  {
    VkDescriptorBufferInfo ubo = {
        .buffer = engine.ubo_host_visible.buffer,
        .offset = 0, // those will be provided at command buffer recording time
        .range  = skinning_matrices_ubo_size,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pBufferInfo     = &ubo,
    };

    write.dstSet = monster_skinning_matrices_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);

    write.dstSet = rig_skinning_matrices_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);
  }

  vec3_set(helmet_translation, -1.0f, 1.0f, 3.0f);
  vec3_set(robot_position, 2.0f, -1.0f, 3.0f);
  vec3_set(rigged_position, 2.0f, 0.0f, 3.0f);

  float extent_width        = static_cast<float>(engine.generic_handles.extent2D.width);
  float extent_height       = static_cast<float>(engine.generic_handles.extent2D.height);
  float aspect_ratio        = extent_width / extent_height;
  float fov                 = to_rad(90.0f);
  float near_clipping_plane = 0.1f;
  float far_clipping_plane  = 1000.0f;
  mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

  VrLevelLoadResult result = level_generator_vr(&engine);

  vr_level_vertex_buffer_offset = result.vertex_target_offset;
  vr_level_index_buffer_offset  = result.index_target_offset;
  vr_level_index_type           = result.index_type;
  vr_level_index_count          = result.index_count;

  SDL_memcpy(vr_level_entry, result.entrance_point, sizeof(vec2));
  SDL_memcpy(vr_level_goal, result.target_goal, sizeof(vec2));

  vr_level_entry[0] *= VR_LEVEL_SCALE;
  vr_level_entry[1] *= VR_LEVEL_SCALE;

  vr_level_goal[0] *= VR_LEVEL_SCALE;
  vr_level_goal[1] *= VR_LEVEL_SCALE;

  vec3_set(player_position, vr_level_entry[0], 2.0f, vr_level_entry[1]);
  quat_identity(player_orientation);

  vec3_set(player_acceleration, 0.0f, 0.0f, 0.0f);
  vec3_set(player_velocity, 0.0f, 0.0f, 0.0f);

  camera_angle        = static_cast<float>(M_PI / 2);
  camera_updown_angle = -1.2f;

  booster_jet_fuel = 1.0f;
}

void Game::teardown(Engine&)
{
  for (SDL_Cursor* cursor : debug_gui.mousecursors)
    SDL_FreeCursor(cursor);
}

void Game::update(Engine& engine, float current_time_sec, float time_delta_since_last_frame)
{
  FunctionTimer timer(update_times, SDL_arraysize(update_times));

  ImGuiIO& io             = ImGui::GetIO();
  bool     quit_requested = false;

  // Event dispatching
  {
    SDL_Event event = {};
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
      case SDL_MOUSEWHEEL:
      {
        if (event.wheel.y > 0.0)
        {
          io.MouseWheel = 1.0f;
        }
        else if (event.wheel.y < 0.0)
        {
          io.MouseWheel = -1.0f;
        }
      }
      break;

      case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(event.text.text);
        break;

      case SDL_MOUSEBUTTONDOWN:
      {
        switch (event.button.button)
        {
        case SDL_BUTTON_LEFT:
          debug_gui.mousepressed[0] = true;
          lmb_clicked               = true;
          SDL_GetMouseState(&lmb_last_cursor_position[0], &lmb_last_cursor_position[1]);
          lmb_current_cursor_position[0] = lmb_last_cursor_position[0];
          lmb_current_cursor_position[1] = lmb_last_cursor_position[1];
          break;
        case SDL_BUTTON_RIGHT:
          debug_gui.mousepressed[1] = true;
          break;
        case SDL_BUTTON_MIDDLE:
          debug_gui.mousepressed[2] = true;
          break;
        default:
          break;
        }
      }
      break;

      case SDL_MOUSEMOTION:
      {
        if (SDL_GetRelativeMouseMode())
        {
          camera_angle -= (0.01f * event.motion.xrel);
          camera_updown_angle -= (0.005f * event.motion.yrel);
        }

        if (lmb_clicked)
        {
          SDL_GetMouseState(&lmb_current_cursor_position[0], &lmb_current_cursor_position[1]);
        }
      }
      break;

      case SDL_MOUSEBUTTONUP:
      {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = false;
        }
      }
      break;

      case SDL_KEYDOWN:
      case SDL_KEYUP:
      {
        io.KeysDown[event.key.keysym.scancode] = (SDL_KEYDOWN == event.type);

        io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
        io.KeyCtrl  = ((SDL_GetModState() & KMOD_CTRL) != 0);
        io.KeyAlt   = ((SDL_GetModState() & KMOD_ALT) != 0);
        io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);

        switch (event.key.keysym.scancode)
        {
        case SDL_SCANCODE_W:
          player_forward_pressed = (SDL_KEYDOWN == event.type);
          break;
        case SDL_SCANCODE_S:
          player_back_pressed = (SDL_KEYDOWN == event.type);
          break;
        case SDL_SCANCODE_A:
          player_strafe_left_pressed = (SDL_KEYDOWN == event.type);
          break;
        case SDL_SCANCODE_D:
          player_strafe_right_pressed = (SDL_KEYDOWN == event.type);
          break;
        case SDL_SCANCODE_LSHIFT:
          player_booster_activated = (SDL_KEYDOWN == event.type);
          break;
        case SDL_SCANCODE_ESCAPE:
          quit_requested = true;
          break;
        case SDL_SCANCODE_F1:
          if (SDL_KEYDOWN == event.type)
            SDL_SetRelativeMouseMode(SDL_TRUE);
          break;
        case SDL_SCANCODE_F2:
          if (SDL_KEYDOWN == event.type)
            SDL_SetRelativeMouseMode(SDL_FALSE);
        default:
          break;
        }
      }

      break;

      default:
        break;
      }
    }
  }

  if (quit_requested)
  {
    SDL_Event event = {.type = SDL_QUIT};
    SDL_PushEvent(&event);
  }

  {
    SDL_Window* window = engine.generic_handles.window;
    int         w, h;
    SDL_GetWindowSize(window, &w, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);

    int    mx, my;
    Uint32 mouseMask               = SDL_GetMouseState(&mx, &my);
    bool   is_mouse_in_window_area = 0 < (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS);
    io.MousePos = is_mouse_in_window_area ? ImVec2((float)mx, (float)my) : ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = debug_gui.mousepressed[0] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)));
    io.MouseDown[1] = debug_gui.mousepressed[1] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)));
    io.MouseDown[2] = debug_gui.mousepressed[2] or (0 != (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)));

    for (bool& iter : debug_gui.mousepressed)
      iter = false;

    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MOUSE_CAPTURE))
      io.MousePos = ImVec2((float)mx, (float)my);

    const bool   any_mouse_button_down     = is_any(io.MouseDown, SDL_arraysize(io.MouseDown));
    const Uint32 window_has_mouse_captured = SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE;

    if (any_mouse_button_down and (not window_has_mouse_captured))
      SDL_CaptureMouse(SDL_TRUE);

    if ((not any_mouse_button_down) and window_has_mouse_captured)
      SDL_CaptureMouse(SDL_FALSE);

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor or (ImGuiMouseCursor_None == cursor))
    {
      SDL_ShowCursor(0);
    }
    else
    {
      SDL_SetCursor(debug_gui.mousecursors[cursor] ? debug_gui.mousecursors[cursor]
                                                   : debug_gui.mousecursors[ImGuiMouseCursor_Arrow]);
      SDL_ShowCursor(1);
    }

    SDL_ShowCursor(io.MouseDrawCursor ? 0 : 1);
  }

  ImGui::NewFrame();
  ImGui::PlotHistogram("update times", update_times, SDL_arraysize(update_times), 0, nullptr, 0.0, 0.001,
                       ImVec2(300, 20));
  ImGui::PlotHistogram("render times", render_times, SDL_arraysize(render_times), 0, nullptr, 0.0, 0.03,
                       ImVec2(300, 20));
  ImGui::Text("Booster jet fluel");
  ImGui::ProgressBar(booster_jet_fuel);
  ImGui::Text("%d %d | %d %d", lmb_last_cursor_position[0], lmb_last_cursor_position[1], lmb_current_cursor_position[0],
              lmb_current_cursor_position[1]);
  ImGui::Text("animation: %s, %.2f", animatedBox.animation_enabled ? "ongoing" : "stopped",
              animatedBox.animation_enabled ? current_time_sec - animatedBox.animation_start_time : 0.0f);

  auto print_animation_stat = [](gltf::RenderableModel& model, float current_time_sec) {
    ImGui::Text("animation: %s, %.2f", model.animation_enabled ? "ongoing" : "stopped",
                model.animation_enabled ? current_time_sec - model.animation_start_time : 0.0f);
  };

  if (ImGui::Button("restart cube animation"))
    restart_animation(animatedBox, current_time_sec);
  print_animation_stat(animatedBox, current_time_sec);

  if (ImGui::Button("restart rigged animation"))
    restart_animation(riggedSimple, current_time_sec);
  print_animation_stat(riggedSimple, current_time_sec);

  if (ImGui::Button("monster animation"))
    restart_animation(monster, current_time_sec);
  print_animation_stat(monster, current_time_sec);

  ImGui::Text("Average update time: %f", avg(update_times, SDL_arraysize(update_times)));
  ImGui::Text("Average render time: %f", avg(render_times, SDL_arraysize(render_times)));

  if (ImGui::Button("quit"))
  {
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
  }

  animate_model(animatedBox, current_time_sec);
  animate_model(riggedSimple, current_time_sec);
  animate_model(monster, current_time_sec);

  for (int i = 0; i < 3; ++i)
  {
    player_position[i] += player_velocity[i] * time_delta_since_last_frame;
    const float friction = 0.2f;
    float       drag     = friction * player_velocity[i];
    player_velocity[i] += player_acceleration[i] * time_delta_since_last_frame;

    if (player_velocity[i])
    {
      player_velocity[i] -= drag;
    }
    else
    {
      player_velocity[i] += drag;
    }

    const float max_speed = 1.0f;
    player_velocity[i]    = clamp(player_velocity[i], -max_speed, max_speed);

    player_acceleration[i] = 0.0f;
  }

  float acceleration = 0.0002f;
  if (player_booster_activated and
      (player_forward_pressed or player_back_pressed or player_strafe_left_pressed or player_strafe_right_pressed))
  {
    if (booster_jet_fuel > 0.0f)
    {
      booster_jet_fuel -= 0.001f;
      acceleration = 0.0006f;
    }
  }

  if (player_forward_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle - (float)M_PI / 2) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle - (float)M_PI / 2) * acceleration;
  }
  else if (player_back_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle + (float)M_PI / 2) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle + (float)M_PI / 2) * acceleration;
  }

  if (player_strafe_left_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle) * acceleration;
  }
  else if (player_strafe_right_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle + (float)M_PI) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle + (float)M_PI) * acceleration;
  }

  float camera_distance = 2.5f;
  float x_camera_offset = SDL_cosf(camera_angle) * camera_distance;
  float y_camera_offset = SDL_sinf(camera_updown_angle) * camera_distance;
  float z_camera_offset = SDL_sinf(camera_angle) * camera_distance;

  camera_position[0] = player_position[0] + x_camera_offset;
  camera_position[1] = y_camera_offset;
  camera_position[2] = player_position[2] - z_camera_offset;

  vec3 center = {player_position[0], 0.0f, player_position[2]};
  vec3 up     = {0.0f, 1.0f, 0.0f};
  mat4x4_look_at(view, camera_position, center, up);

  ImGui::Text("position:     %.2f %.2f %.2f", player_position[0], player_position[1], player_position[2]);
  ImGui::Text("camera:       %.2f %.2f %.2f", camera_position[0], camera_position[1], camera_position[2]);
  ImGui::Text("acceleration: %.2f %.2f %.2f", player_acceleration[0], player_acceleration[1], player_acceleration[2]);
  ImGui::Text("velocity:     %.2f %.2f %.2f", player_velocity[0], player_velocity[1], player_velocity[2]);
  ImGui::Text("time:         %.4f", current_time_sec);

  ImGui::Text("WASD - movement");
  ImGui::Text("F1 - enable first person view");
  ImGui::Text("F2 - disable first person view");
  ImGui::Text("ESC - exit");

  {
    struct Light
    {
    public:
      Light(vec4 position, vec4 color)
          : p(position)
          , c(color)
      {
      }

      void x(float val)
      {
        p[0] = val;
      }

      void y(float val)
      {
        p[1] = val;
      }

      void z(float val)
      {
        p[2] = val;
      }

      void rgb(float r, float g, float b)
      {
        c[0] = r;
        c[1] = g;
        c[2] = b;
      }

    private:
      float* p;
      float* c;
    };

    pbr_light_sources_cache.count = 5;
    vec4* p                       = pbr_light_sources_cache.positions;
    vec4* c                       = pbr_light_sources_cache.colors;

    Light l0(p[0], c[0]);
    l0.x(SDL_sinf(current_time_sec));
    l0.y(-0.5f);
    l0.z(3.0f + SDL_cosf(current_time_sec));
    l0.rgb(20.0, 0.0, 0.0);

    Light l1(p[1], c[1]);
    l1.x(0.8f * SDL_cosf(current_time_sec));
    l1.y(-0.6f);
    l1.z(3.0f + (0.8f * SDL_sinf(current_time_sec)));
    l1.rgb(0.0, 20.0, 0.0);

    Light l2(p[2], c[2]);
    l2.x(0.8f * SDL_sinf(current_time_sec / 2.0f));
    l2.y(-0.3f);
    l2.z(3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f)));
    l2.rgb(0.0, 0.0, 20.0);

    Light l3(p[3], c[3]);
    l3.x(SDL_sinf(current_time_sec / 1.2f));
    l3.y(-0.1f);
    l3.z(2.5f * SDL_cosf(current_time_sec / 1.2f));
    l3.rgb(8.0, 8.0, 8.0);

    Light l4(p[4], c[4]);
    l4.x(0.0f);
    l4.y(-1.0f);
    l4.z(4.0f);
    l4.rgb(10.0, 0.0, 10.0);
  }
}

void Game::render(Engine& engine, float current_time_sec)
{
  FunctionTimer            timer(render_times, SDL_arraysize(render_times));
  Engine::SimpleRendering& renderer = engine.simple_rendering;

  uint32_t image_index = 0;
  vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                        engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
  vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);

  update_ubo(engine.generic_handles.device, engine.ubo_host_visible.memory, sizeof(LightSources),
             pbr_dynamic_lights_ubo_offsets[image_index], &pbr_light_sources_cache);

  CommandBufferSelector command_selector(engine.simple_rendering, image_index);
  CommandBufferStarter  command_starter(renderer.render_pass, renderer.framebuffers[image_index]);

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Pipeline::Skybox);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Pass::Skybox);

    struct
    {
      mat4x4 projection;
      mat4x4 view;
    } push = {};

    mat4x4_dup(push.projection, projection);
    mat4x4_dup(push.view, view);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Pipeline::Skybox]);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox], 0, 1,
                            &skybox_cubemap_dset, 0, nullptr);

    vkCmdPushConstants(cmd, renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    box.renderRaw(engine, cmd);
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Pipeline::Scene3D);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Pass::Objects3D);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

    {
      VkDescriptorSet dsets[]           = {pbr_ibl_environment_dset, pbr_dynamic_lights_dset};
      uint32_t        dynamic_offsets[] = {static_cast<uint32_t>(pbr_dynamic_lights_ubo_offsets[image_index])};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 1,
                              SDL_arraysize(dsets), dsets, SDL_arraysize(dynamic_offsets), dynamic_offsets);
    }

    {
      Quaternion orientation;
      orientation.rotateX(to_rad(180.0));

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, vr_level_goal[0], 0.0f, vr_level_goal[1]);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, 1.6f, 1.6f, 1.6f);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, tmp, scale_matrix);

      vec3 color = {0.0f, 0.0f, 0.0f};

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0, 1,
                              &helmet_pbr_material_dset, 0, nullptr);

      helmet.renderColored(engine, cmd, projection, view, world_transform, color,
                           Engine::SimpleRendering::Pipeline::Scene3D, 0, camera_position);
    }

    {
      Quaternion orientation;

      {
        Quaternion standing_pose;
        standing_pose.rotateX(to_rad(180.0));

        Quaternion rotate_back;
        rotate_back.rotateY(player_position[0] < camera_position[0] ? to_rad(90.0f) : -to_rad(90.0f));

        float      x_delta = player_position[0] - camera_position[0];
        float      z_delta = player_position[2] - camera_position[2];
        Quaternion camera;
        camera.rotateY(static_cast<float>(SDL_atan(z_delta / x_delta)));

        orientation = standing_pose * rotate_back * camera;
      }

      vec3 color = {0.0f, 0.0f, 0.0f};

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, player_position[0], player_position[1] - 1.0f, player_position[2]);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, tmp, scale_matrix);

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0, 1,
                              &robot_pbr_material_dset, 0, nullptr);

      robot.renderColored(engine, cmd, projection, view, world_transform, color,
                          Engine::SimpleRendering::Pipeline::Scene3D, 0, camera_position);
    }
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Pipeline::ColoredGeometry);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Pass::Objects3D);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

    for (int i = 0; i < pbr_light_sources_cache.count; ++i)
    {
      Quaternion orientation = Quaternion().rotateZ(to_rad(100.0f * current_time_sec)) *
                               Quaternion().rotateY(to_rad(280.0f * current_time_sec)) *
                               Quaternion().rotateX(to_rad(60.0f * current_time_sec));

      float* position           = pbr_light_sources_cache.positions[i];
      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, position[0], position[1], position[2]);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.05f, 0.05f, 0.05f);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, tmp, scale_matrix);

      float* color = pbr_light_sources_cache.colors[i];
      box.renderColored(engine, cmd, projection, view, world_transform, color,
                        Engine::SimpleRendering::Pipeline::ColoredGeometry, 0, camera_position);
    }

    {
      Quaternion orientation = Quaternion().rotateZ(to_rad(90.0f * current_time_sec / 90.0f)) *
                               Quaternion().rotateY(to_rad(140.0f * current_time_sec / 30.0f)) *
                               Quaternion().rotateX(to_rad(90.0f * current_time_sec / 20.0f));

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, robot_position[0], robot_position[1], robot_position[2]);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, translation_matrix, rotation_matrix);

      vec3 color = {0.0, 1.0, 0.0};
      animatedBox.renderColored(engine, cmd, projection, view, world_transform, color,
                                Engine::SimpleRendering::Pipeline::ColoredGeometry, 0, camera_position);
    }

    {
      mat4x4 projection_view = {};
      mat4x4_mul(projection_view, projection, view);

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, 0.0, 2.5, 0.0);

      mat4x4 rotation_matrix = {};
      mat4x4_identity(rotation_matrix);

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, VR_LEVEL_SCALE, VR_LEVEL_SCALE, VR_LEVEL_SCALE);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);

      mat4x4 model = {};
      mat4x4_mul(model, tmp, scale_matrix);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projection_view, model);

      vkCmdBindIndexBuffer(cmd, engine.gpu_static_geometry.buffer, vr_level_index_buffer_offset, vr_level_index_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_static_geometry.buffer, &vr_level_vertex_buffer_offset);

      vec3 color = {0.5, 0.5, 1.0};
      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                         VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

      vkCmdDrawIndexed(cmd, static_cast<uint32_t>(vr_level_index_count), 1, 0, 0, 0);
    }
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Pass::Objects3D);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

    {
      Quaternion orientation;
      orientation.rotateX(to_rad(45.0f));

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, rigged_position[0], rigged_position[1], rigged_position[2]);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, translation_matrix, rotation_matrix);

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, tmp, scale_matrix);

      vec3 color = {0.0, 0.0, 1.0};

      uint32_t dynamic_offsets[] = {static_cast<uint32_t>(rig_skinning_matrices_ubo_offsets[image_index])};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0,
                              1, &rig_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

      riggedSimple.renderColored(engine, cmd, projection, view, world_transform, color,
                                 Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
                                 rig_skinning_matrices_ubo_offsets[image_index], camera_position);
    }

    {
      Quaternion orientation;
      orientation.rotateX(to_rad(45.0f));

      mat4x4 translation_matrix = {};
      mat4x4_translate(translation_matrix, 2.0f, 0.5f, 0.5f);

      mat4x4 rotation_matrix = {};
      mat4x4_from_quat(rotation_matrix, orientation.data());

      mat4x4 scale_matrix = {};
      mat4x4_identity(scale_matrix);
      float factor = 0.025f;
      mat4x4_scale_aniso(scale_matrix, scale_matrix, factor, factor, factor);

      mat4x4 tmp = {};
      mat4x4_mul(tmp, rotation_matrix, translation_matrix);

      mat4x4 world_transform = {};
      mat4x4_mul(world_transform, tmp, scale_matrix);

      vec3 color = {1.0, 1.0, 1.0};

      uint32_t dynamic_offsets[] = {static_cast<uint32_t>(monster_skinning_matrices_ubo_offsets[image_index])};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0,
                              1, &monster_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

      monster.renderColored(engine, cmd, projection, view, world_transform, color,
                            Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
                            monster_skinning_matrices_ubo_offsets[image_index], camera_position);
    }
  }

  {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGuiIO&    io        = ImGui::GetIO();

    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    SDL_assert(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
    SDL_assert(DebugGui::INDEX_BUFFER_CAPACITY_BYTES >= index_size);

    if (0 < vertex_size)
    {
      ScopedMemoryMap memory_map(engine.generic_handles.device, engine.gpu_host_visible.memory,
                                 debug_gui.vertex_buffer_offsets[image_index], vertex_size);

      ImDrawVert* vtx_dst = memory_map.get<ImDrawVert>();
      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx_dst += cmd_list->VtxBuffer.Size;
      }
    }

    if (0 < index_size)
    {
      ScopedMemoryMap memory_map(engine.generic_handles.device, engine.gpu_host_visible.memory,
                                 debug_gui.index_buffer_offsets[image_index], index_size);

      ImDrawIdx* idx_dst = memory_map.get<ImDrawIdx>();
      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
      }
    }

    VkCommandBuffer command_buffer = command_selector.select(Engine::SimpleRendering::Pipeline::ImGui);
    ScopedCommand   cmd_scope      = command_starter.begin(command_buffer, Engine::SimpleRendering::Pass::ImGui);

    if (vertex_size and index_size)
    {
      vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderer.pipelines[Engine::SimpleRendering::Pipeline::ImGui]);

      vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui], 0, 1,
                              &imgui_font_atlas_dset, 0, nullptr);

      vkCmdBindIndexBuffer(command_buffer, engine.gpu_host_visible.buffer, debug_gui.index_buffer_offsets[image_index],
                           VK_INDEX_TYPE_UINT16);

      vkCmdBindVertexBuffers(command_buffer, 0, 1, &engine.gpu_host_visible.buffer,
                             &debug_gui.vertex_buffer_offsets[image_index]);

      {
        VkViewport viewport = {
            .width    = io.DisplaySize.x,
            .height   = io.DisplaySize.y,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
      }

      float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
      float translate[] = {-1.0f, -1.0f};

      vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, scale);
      vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                         VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

      {
        int vtx_offset = 0;
        int idx_offset = 0;

        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
          const ImDrawList* cmd_list = draw_data->CmdLists[n];
          for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
          {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
              pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
              {
                VkRect2D scissor = {
                    .offset =
                        {
                            .x = (int32_t)(pcmd->ClipRect.x) > 0 ? (int32_t)(pcmd->ClipRect.x) : 0,
                            .y = (int32_t)(pcmd->ClipRect.y) > 0 ? (int32_t)(pcmd->ClipRect.y) : 0,
                        },
                    .extent =
                        {
                            .width  = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                            .height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1) // FIXME: Why +1 here?
                        },
                };
                vkCmdSetScissor(command_buffer, 0, 1, &scissor);
              }
              vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, static_cast<uint32_t>(idx_offset), vtx_offset, 0);
            }
            idx_offset += pcmd->ElemCount;
          }
          vtx_offset += cmd_list->VtxBuffer.Size;
        }
      }
    }
  }

  engine.submit_simple_rendering(image_index);
}
