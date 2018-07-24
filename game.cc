#include "game.hh"
#include "cubemap.hh"
#include "pipelines.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>

#define VR_LEVEL_SCALE 100.0f

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

float clamp(float val, float min, float max)
{
  return (val < min) ? min : (val > max) ? max : val;
}

int find_first_higher(const float times[], float current)
{
  int iter = 0;
  while (current > times[iter])
    iter += 1;
  return iter;
}

void lerp(const float a[], const float b[], float result[], int dim, float t)
{
  for (int i = 0; i < dim; ++i)
  {
    float difference = b[i] - a[i];
    float progressed = difference * t;
    result[i]        = a[i] + progressed;
  }
}

//
// https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#appendix-c-spline-interpolation
//
void hermite_cubic_spline_interpolation(const float a_in[], const float b_in[], float result[], int dim, float t,
                                        float total_duration)
{
  const float* a_spline_vertex = &a_in[dim];
  const float* a_out_tangent   = &a_in[2 * dim];

  const float* b_in_tangent    = &b_in[0];
  const float* b_spline_vertex = &b_in[dim];

  for (int i = 0; i < dim; ++i)
  {
    float P[2] = {a_spline_vertex[i], b_spline_vertex[i]};
    float M[2] = {a_out_tangent[i], b_in_tangent[i]};

    for (float& m : M)
      m *= total_duration;

    float a   = (2.0f * P[0]) + M[0] + (-2.0f * P[1]) + M[1];
    float b   = (-3.0f * P[0]) - (2.0f * M[0]) + (3.0f * P[1]) - M[1];
    float c   = M[0];
    float d   = P[0];
    result[i] = (a * t * t * t) + (b * t * t) + (c * t) + (d);
  }
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
        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[4 * keyframe_lower];
          float* b = &sampler.values[4 * keyframe_upper];
          float* c = model.animation_rotations[channel.target_node_idx];
          lerp(a, b, c, 4, keyframe_uniform_time);
          vec4_norm(c, c);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = model.animation_rotations[channel.target_node_idx];
          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
          vec4_norm(c, c);
        }

        model.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[3 * keyframe_lower];
          float* b = &sampler.values[3 * keyframe_upper];
          float* c = model.animation_translations[channel.target_node_idx];
          lerp(a, b, c, 3, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = model.animation_translations[channel.target_node_idx];
          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
          model.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
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

#if 0
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
#endif

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

GuiLineSizeCount count_lines(const ArrayView<GuiLine>& lines, const GuiLine::Color color)
{
  GuiLineSizeCount r = {};

  for (const GuiLine& line : lines)
  {
    if (color == line.color)
    {
      switch (line.size)
      {
      case GuiLine::Size::Big:
        r.big++;
        break;
      case GuiLine::Size::Normal:
        r.normal++;
        break;
      case GuiLine::Size::Small:
        r.small++;
        break;
      case GuiLine::Size::Tiny:
        r.tiny++;
        break;
      }
    }
  }

  return r;
}

uint32_t line_to_pixel_length(float coord, int pixel_max_size)
{
  return static_cast<uint32_t>((coord * pixel_max_size * 0.5f));
}

} // namespace

// game_generate_gui_lines.cc
void generate_gui_lines(const GenerateGuiLinesCommand& cmd, GuiLine* dst, int* count);
void generate_gui_height_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count);
void generate_gui_tilt_ruler_text(struct GenerateGuiLinesCommand& cmd, GuiHeightRulerText* dst, int* count);

// game_generate_sdf_font.cc
GenerateSdfFontCommandResult generate_sdf_font(const GenerateSdfFontCommand& cmd);

// game_generate_sdl_imgui_mappings.cc
ArrayView<KeyMapping>    generate_sdl_imgui_keymap(Engine::DoubleEndedStack& allocator);
ArrayView<CursorMapping> generate_sdl_imgui_cursormap(Engine::DoubleEndedStack& allocator);

// game_recalculate_node_transforms.cc
void recalculate_node_transforms(Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                 mat4x4 world_transform);
void recalculate_skinning_matrices(const Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                   mat4x4 world_transform);

// game_render_entity.cc
void render_pbr_entity(Entity entity, EntityComponentSystem &ecs, gltf::RenderableModel &model, Engine &engine,
                       RenderEntityParams &p);
void render_entity(Entity entity, EntityComponentSystem& ecs, gltf::RenderableModel& model, Engine& engine,
                   RenderEntityParams& p);

namespace {

struct WorkerThreadData
{
  Engine& engine;
  Game&   game;
};

int render_skybox_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Skybox;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Skybox,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  struct
  {
    mat4x4 projection;
    mat4x4 view;
  } push = {};

  mat4x4_dup(push.projection, tjd.game.projection);
  mat4x4_dup(push.view, tjd.game.view);

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Skybox]);

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox], 0, 1,
                          &tjd.game.skybox_cubemap_dset, 0, nullptr);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

  tjd.game.box.renderRaw(tjd.engine, tjd.command);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_robot_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  {
    VkDescriptorSet dsets[]    = {tjd.game.pbr_ibl_environment_dset, tjd.game.pbr_dynamic_lights_dset};
    uint32_t dynamic_offsets[] = {static_cast<uint32_t>(tjd.game.pbr_dynamic_lights_ubo_offsets[tjd.game.image_index])};

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 1,
                            SDL_arraysize(dsets), dsets, SDL_arraysize(dynamic_offsets), dynamic_offsets);
  }

  {
    Quaternion orientation;

    {
      Quaternion standing_pose;
      standing_pose.rotateX(to_rad(180.0));

      Quaternion rotate_back;
      rotate_back.rotateY(tjd.game.player_position[0] < tjd.game.camera_position[0] ? to_rad(180.0f) : to_rad(0.0f));

      float      x_delta = tjd.game.player_position[0] - tjd.game.camera_position[0];
      float      z_delta = tjd.game.player_position[2] - tjd.game.camera_position[2];
      Quaternion camera;
      camera.rotateY(static_cast<float>(SDL_atan(z_delta / x_delta)));

      orientation = standing_pose * rotate_back * camera;
    }

    vec3 color = {0.0f, 0.0f, 0.0f};

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, tjd.game.player_position[0], tjd.game.player_position[1] - 1.0f,
                     tjd.game.player_position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);

    mat4x4 world_transform = {};
    mat4x4_mul(world_transform, tmp, scale_matrix);

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0,
                            1, &tjd.game.robot_pbr_material_dset, 0, nullptr);

    tjd.game.robot.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform, color,
                                 Engine::SimpleRendering::Pipeline::Scene3D, 0, tjd.game.camera_position);
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_helmet_job(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  {
    VkDescriptorSet dsets[]    = {tjd.game.pbr_ibl_environment_dset, tjd.game.pbr_dynamic_lights_dset};
    uint32_t dynamic_offsets[] = {static_cast<uint32_t>(tjd.game.pbr_dynamic_lights_ubo_offsets[tjd.game.image_index])};

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 1,
                            SDL_arraysize(dsets), dsets, SDL_arraysize(dynamic_offsets), dynamic_offsets);
  }

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D], 0,
                          1, &tjd.game.helmet_pbr_material_dset, 0, nullptr);

  RenderEntityParams params = {
      .cmd      = tjd.command,
      .color    = {0.0f, 0.0f, 0.0f},
      .pipeline = Engine::SimpleRendering::Pipeline::Scene3D,
  };

  mat4x4_dup(params.projection, tjd.game.projection);
  mat4x4_dup(params.view, tjd.game.view);
  SDL_memcpy(params.camera_position, tjd.game.camera_position, sizeof(vec3));
  render_pbr_entity(tjd.game.helmet_entity, tjd.game.ecs, tjd.game.helmet, tjd.engine, params);

  //tjd.game.helmet.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform, color,
                                //Engine::SimpleRendering::Pipeline::Scene3D, 0, tjd.game.camera_position);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_point_light_boxes(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  for (int i = 0; i < tjd.game.pbr_light_sources_cache.count; ++i)
  {
    Quaternion orientation = Quaternion().rotateZ(to_rad(100.0f * tjd.game.current_time_sec)) *
                             Quaternion().rotateY(to_rad(280.0f * tjd.game.current_time_sec)) *
                             Quaternion().rotateX(to_rad(60.0f * tjd.game.current_time_sec));

    float* position           = tjd.game.pbr_light_sources_cache.positions[i];
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

    float* color = tjd.game.pbr_light_sources_cache.colors[i];
    tjd.game.box.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform, color,
                               Engine::SimpleRendering::Pipeline::ColoredGeometry, 0, tjd.game.camera_position);
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_matrioshka_box(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  {
    Quaternion orientation = Quaternion().rotateZ(to_rad(90.0f * tjd.game.current_time_sec / 90.0f)) *
                             Quaternion().rotateY(to_rad(140.0f * tjd.game.current_time_sec / 30.0f)) *
                             Quaternion().rotateX(to_rad(90.0f * tjd.game.current_time_sec / 20.0f));

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, tjd.game.robot_position[0], tjd.game.robot_position[1],
                     tjd.game.robot_position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 world_transform = {};
    mat4x4_mul(world_transform, translation_matrix, rotation_matrix);

    vec3 color = {0.0, 1.0, 0.0};
    tjd.game.animatedBox.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform,
                                       color, Engine::SimpleRendering::Pipeline::ColoredGeometry, 0,
                                       tjd.game.camera_position);
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_vr_scene(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, tjd.game.projection, tjd.game.view);

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

  vkCmdBindIndexBuffer(tjd.command, tjd.engine.gpu_static_geometry.buffer, tjd.game.vr_level_index_buffer_offset,
                       tjd.game.vr_level_index_type);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.vr_level_vertex_buffer_offset);

  vec3 color = {0.5, 0.5, 1.0};
  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

  vkCmdDrawIndexed(tjd.command, static_cast<uint32_t>(tjd.game.vr_level_index_count), 1, 0, 0, 0);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_simple_rigged(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  Quaternion orientation;
  orientation.rotateX(to_rad(45.0f));

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.rigged_position[0], tjd.game.rigged_position[1],
                   tjd.game.rigged_position[2]);

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

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(tjd.game.rig_skinning_matrices_ubo_offsets[tjd.game.image_index])};

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0, 1,
      &tjd.game.rig_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  tjd.game.riggedSimple.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform,
                                      color, Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
                                      tjd.game.rig_skinning_matrices_ubo_offsets[tjd.game.image_index],
                                      tjd.game.camera_position);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_monster_rigged(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::Objects3D;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::Objects3D,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  Quaternion orientation;
  orientation.rotateX(to_rad(45.0f));

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, -2.0f, 0.5f, 0.5f);

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

  uint32_t dynamic_offsets[] = {
      static_cast<uint32_t>(tjd.game.monster_skinning_matrices_ubo_offsets[tjd.game.image_index])};
  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned], 0, 1,
      &tjd.game.monster_skinning_matrices_dset, SDL_arraysize(dynamic_offsets), dynamic_offsets);

  tjd.game.monster.renderColored(tjd.engine, tjd.command, tjd.game.projection, tjd.game.view, world_transform, color,
                                 Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned,
                                 tjd.game.monster_skinning_matrices_ubo_offsets[tjd.game.image_index],
                                 tjd.game.camera_position);

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_radar(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGui]);

  vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui], 0,
                          1, &tjd.game.radar_texture_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

  mat4x4 gui_projection = {};

  {
    float extent_width        = static_cast<float>(tjd.engine.generic_handles.extent2D.width);
    float extent_height       = static_cast<float>(tjd.engine.generic_handles.extent2D.height);
    float aspect_ratio        = extent_width / extent_height;
    float fov                 = to_rad(90.0f);
    float near_clipping_plane = 0.001f;
    float far_clipping_plane  = 100.0f;
    mat4x4_perspective(gui_projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
    gui_projection[1][1] *= -1.0f;
  }

  mat4x4 gui_view = {};

  {
    vec3 center   = {0.0f, 0.0f, 0.0f};
    vec3 up       = {0.0f, -1.0f, 0.0f};
    vec3 position = {0.0f, 0.0f, -10.0f};
    mat4x4_look_at(gui_view, position, center, up);
  }

  Quaternion orientation;
  orientation.rotateY(to_rad(tjd.game.green_gui_radar_rotation));

  mat4x4 translation_matrix = {};
  mat4x4_translate(translation_matrix, tjd.game.green_gui_radar_position[0], tjd.game.green_gui_radar_position[1],
                   0.0f);

  mat4x4 rotation_matrix = {};
  mat4x4_from_quat(rotation_matrix, orientation.data());

  mat4x4 scale_matrix = {};
  mat4x4_identity(scale_matrix);
  mat4x4_scale_aniso(scale_matrix, scale_matrix, 2.0f, 2.0f, 1.0f);

  mat4x4 tmp = {};
  mat4x4_mul(tmp, translation_matrix, rotation_matrix);

  mat4x4 world_transform = {};
  mat4x4_mul(world_transform, tmp, scale_matrix);

  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, gui_projection, gui_view);

  mat4x4 mvp = {};
  mat4x4_mul(mvp, projection_view, world_transform);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui],
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

  vkCmdPushConstants(tjd.command,
                     tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui],
                     VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(float), &tjd.game.current_time_sec);

  vkCmdDraw(tjd.command, 4, 1, 0, 0);
  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_robot_gui_lines(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiLines]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines], 0, 1,
      &tjd.game.radar_texture_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_host_visible.buffer,
                         &tjd.game.green_gui_rulers_buffer_offsets[tjd.game.image_index]);

  {
    VkRect2D scissor{};
    scissor.extent.width  = line_to_pixel_length(0.75f, tjd.engine.generic_handles.extent2D.width);
    scissor.extent.height = line_to_pixel_length(1.02f, tjd.engine.generic_handles.extent2D.height);
    scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = line_to_pixel_length(0.29f, tjd.engine.generic_handles.extent2D.height); // 118
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);
  }

  uint32_t offset = 0;

  // ------ GREEN ------
  {
    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_green_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {125.0f / 255.0f, 204.0f / 255.0f, 174.0f / 255.0f, 0.9f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ RED ------
  {
    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_red_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {1.0f, 0.0f, 0.0f, 0.9f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  // ------ YELLOW ------
  {
    VkRect2D scissor      = {};
    scissor.extent.width  = line_to_pixel_length(0.5f, tjd.engine.generic_handles.extent2D.width);
    scissor.extent.height = line_to_pixel_length(1.3f, tjd.engine.generic_handles.extent2D.height);
    scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
    scissor.offset.y      = line_to_pixel_length(0.2f, tjd.engine.generic_handles.extent2D.height);
    vkCmdSetScissor(tjd.command, 0, 1, &scissor);

    const float             line_widths[] = {7.0f, 5.0f, 3.0f, 1.0f};
    const GuiLineSizeCount& counts        = tjd.game.gui_yellow_lines_count;
    const int               line_counts[] = {counts.big, counts.normal, counts.small, counts.tiny};

    vec4 color = {1.0f, 1.0f, 0.0f, 0.7f};
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), color);

    for (int i = 0; i < 4; ++i)
    {
      if (0 == line_counts[i])
        continue;

      vkCmdSetLineWidth(tjd.command, line_widths[i]);
      vkCmdDraw(tjd.command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
      offset += line_counts[i];
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_height_ruler_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    mat4x4 mvp;
    vec2   character_coordinate;
    vec2   character_size;
  } vpc = {};

  struct FragmentPushConstant
  {
    vec3  color;
    float time;
  } fpc = {};

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // height rulers values
  //--------------------------------------------------------------------------
  ArrayView<GuiHeightRulerText> scheduled_text_data = {};

  {
    GenerateGuiLinesCommand cmd = {
        .player_y_location_meters = -(2.0f - tjd.game.player_position[1]),
        .camera_x_pitch_radians   = tjd.game.camera_angle,
        .camera_y_pitch_radians   = tjd.game.camera_updown_angle,
        .screen_extent2D          = tjd.engine.generic_handles.extent2D,
    };

    generate_gui_height_ruler_text(cmd, nullptr, &scheduled_text_data.count);
    scheduled_text_data.data = tjd.allocator.allocate<GuiHeightRulerText>(scheduled_text_data.count);
    generate_gui_height_ruler_text(cmd, scheduled_text_data.data, &scheduled_text_data.count);
  }

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    mat4x4 gui_projection = {};
    mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
                 tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset[0], text.offset[1], -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = line_to_pixel_length(0.75f, tjd.engine.generic_handles.extent2D.width);
      scissor.extent.height = line_to_pixel_length(1.02f, tjd.engine.generic_handles.extent2D.height);
      scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = line_to_pixel_length(0.29f, tjd.engine.generic_handles.extent2D.height); // 118
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 0.0f;
      fpc.color[2] = 0.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_tilt_ruler_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    mat4x4 mvp;
    vec2   character_coordinate;
    vec2   character_size;
  } vpc = {};

  struct FragmentPushConstant
  {
    vec3  color;
    float time;
  } fpc = {};

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // tilt rulers values
  //--------------------------------------------------------------------------
  ArrayView<GuiHeightRulerText> scheduled_text_data = {};

  {
    GenerateGuiLinesCommand cmd = {
        .player_y_location_meters = -(2.0f - tjd.game.player_position[1]),
        .camera_x_pitch_radians   = tjd.game.camera_angle,
        .camera_y_pitch_radians   = tjd.game.camera_updown_angle,
        .screen_extent2D          = tjd.engine.generic_handles.extent2D,
    };

    generate_gui_tilt_ruler_text(cmd, nullptr, &scheduled_text_data.count);
    scheduled_text_data.data = tjd.allocator.allocate<GuiHeightRulerText>(scheduled_text_data.count);
    generate_gui_tilt_ruler_text(cmd, scheduled_text_data.data, &scheduled_text_data.count);
  }

  char buffer[256];
  for (GuiHeightRulerText& text : scheduled_text_data)
  {
    mat4x4 gui_projection = {};
    mat4x4_ortho(gui_projection, 0, tjd.engine.generic_handles.extent2D.width, 0,
                 tjd.engine.generic_handles.extent2D.height, 0.0f, 1.0f);

    float cursor = 0.0f;

    const int length = SDL_snprintf(buffer, 256, "%d", text.value);
    for (int i = 0; i < length; ++i)
    {
      GenerateSdfFontCommand cmd = {
          .character             = buffer[i],
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = static_cast<float>(text.size),
          .position              = {text.offset[0], text.offset[1], -1.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, gui_projection, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor{};
      scissor.extent.width  = line_to_pixel_length(0.5f, tjd.engine.generic_handles.extent2D.width);
      scissor.extent.height = line_to_pixel_length(1.3f, tjd.engine.generic_handles.extent2D.height);
      scissor.offset.x      = (tjd.engine.generic_handles.extent2D.width / 2) - (scissor.extent.width / 2);
      scissor.offset.y      = line_to_pixel_length(0.2f, tjd.engine.generic_handles.extent2D.height);
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 1.0f;
      fpc.color[2] = 0.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_hello_world_text(ThreadJobData tjd)
{
  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  vkCmdBindDescriptorSets(
      tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont], 0, 1,
      &tjd.game.lucida_sans_sdf_dset, 0, nullptr);

  vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_static_geometry.buffer,
                         &tjd.game.green_gui_billboard_vertex_buffer_offset);

  struct VertexPushConstant
  {
    mat4x4 mvp;
    vec2   character_coordinate;
    vec2   character_size;
  } vpc = {};

  struct FragmentPushConstant
  {
    vec3  color;
    float time;
  } fpc = {};

  fpc.time = tjd.game.current_time_sec;

  //--------------------------------------------------------------------------
  // 3D rotating text demo
  //--------------------------------------------------------------------------
  {
    mat4x4 gui_projection = {};

    {
      float extent_width        = static_cast<float>(tjd.engine.generic_handles.extent2D.width);
      float extent_height       = static_cast<float>(tjd.engine.generic_handles.extent2D.height);
      float aspect_ratio        = extent_width / extent_height;
      float fov                 = to_rad(90.0f);
      float near_clipping_plane = 0.001f;
      float far_clipping_plane  = 100.0f;
      mat4x4_perspective(gui_projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
      gui_projection[1][1] *= -1.0f;
    }

    mat4x4 gui_view = {};

    {
      vec3 center   = {0.0f, 0.0f, 0.0f};
      vec3 up       = {0.0f, -1.0f, 0.0f};
      vec3 position = {0.0f, 0.0f, -10.0f};
      mat4x4_look_at(gui_view, position, center, up);
    }

    mat4x4 projection_view = {};
    mat4x4_mul(projection_view, gui_projection, gui_view);

    float      cursor = 0.0f;
    const char word[] = "Hello World!";

    for (const char c : word)
    {
      if ('\0' == c)
        continue;

      GenerateSdfFontCommand cmd = {
          .character             = c,
          .lookup_table          = tjd.game.lucida_sans_sdf_char_ids,
          .character_data        = tjd.game.lucida_sans_sdf_chars,
          .characters_pool_count = SDL_arraysize(tjd.game.lucida_sans_sdf_char_ids),
          .texture_size          = {512, 256},
          .scaling               = 30.0f,
          .position              = {2.0f, 6.0f, 0.0f},
          .cursor                = cursor,
      };

      GenerateSdfFontCommandResult r = generate_sdf_font(cmd);

      SDL_memcpy(vpc.character_coordinate, r.character_coordinate, sizeof(vec2));
      SDL_memcpy(vpc.character_size, r.character_size, sizeof(vec2));
      mat4x4_mul(vpc.mvp, projection_view, r.transform);
      cursor += r.cursor_movement;

      VkRect2D scissor = {.extent = tjd.engine.generic_handles.extent2D};
      vkCmdSetScissor(tjd.command, 0, 1, &scissor);

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vpc), &vpc);

      fpc.color[0] = 1.0f;
      fpc.color[1] = 1.0f;
      fpc.color[2] = 1.0f;

      vkCmdPushConstants(
          tjd.command, tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
          VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vpc), sizeof(fpc), &fpc);

      vkCmdDraw(tjd.command, 4, 1, 0, 0);
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

int render_imgui(ThreadJobData tjd)
{
  ImDrawData* draw_data = ImGui::GetDrawData();
  ImGuiIO&    io        = ImGui::GetIO();

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if ((0 == vertex_size) or (0 == index_size))
    return -1;

  RecordedCommandBuffer& result = tjd.game.js_sink.commands[SDL_AtomicIncRef(&tjd.game.js_sink.count)];
  result.command                = tjd.command;
  result.subpass                = Engine::SimpleRendering::Pass::ImGui;

  {
    VkCommandBufferInheritanceInfo inheritance = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = tjd.engine.simple_rendering.render_pass,
        .subpass     = Engine::SimpleRendering::Pass::ImGui,
        .framebuffer = tjd.engine.simple_rendering.framebuffers[tjd.game.image_index],
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance,
    };

    vkBeginCommandBuffer(tjd.command, &begin_info);
  }

  if (vertex_size and index_size)
  {
    vkCmdBindPipeline(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tjd.engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ImGui]);

    vkCmdBindDescriptorSets(tjd.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui], 0,
                            1, &tjd.game.imgui_font_atlas_dset, 0, nullptr);

    vkCmdBindIndexBuffer(tjd.command, tjd.engine.gpu_host_visible.buffer,
                         tjd.game.debug_gui.index_buffer_offsets[tjd.game.image_index], VK_INDEX_TYPE_UINT16);

    vkCmdBindVertexBuffers(tjd.command, 0, 1, &tjd.engine.gpu_host_visible.buffer,
                           &tjd.game.debug_gui.vertex_buffer_offsets[tjd.game.image_index]);

    {
      VkViewport viewport = {
          .width    = io.DisplaySize.x,
          .height   = io.DisplaySize.y,
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };
      vkCmdSetViewport(tjd.command, 0, 1, &viewport);
    }

    float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    float translate[] = {-1.0f, -1.0f};

    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, scale);
    vkCmdPushConstants(tjd.command,
                       tjd.engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

    vkCmdPipelineBarrier(tjd.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);

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
            vkCmdSetScissor(tjd.command, 0, 1, &scissor);
            vkCmdDrawIndexed(tjd.command, pcmd->ElemCount, 1, static_cast<uint32_t>(idx_offset), vtx_offset, 0);
          }
          idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
      }
    }
  }

  vkEndCommandBuffer(tjd.command);
  return 0;
}

void worker_function(WorkerThreadData td)
{
  JobSystem& job_system = td.game.js;

  int threadId = SDL_AtomicIncRef(&job_system.threads_finished_work);
  if ((SDL_arraysize(job_system.worker_threads) - 1) == threadId)
    SDL_SemPost(job_system.all_threads_idle_signal);

  VkCommandBuffer* all_commands                 = td.game.js.worker_commands[threadId].commands;
  VkCommandBuffer* just_recorded_commands       = &all_commands[0];
  VkCommandBuffer* recorded_last_frame_commands = &all_commands[64];
  VkCommandBuffer* ready_to_reset_commands      = &all_commands[128];
  int              just_recorded_count          = 0;
  int              recorded_last_frame_count    = 0;
  int              ready_to_reset_count         = 0;

  LinearAllocator allocator(1024);

  while (not job_system.thread_end_requested)
  {
    //
    // As a proof of concept this signal will always be broadcasted on next render frame.
    //
    SDL_LockMutex(job_system.new_jobs_available_mutex);
    SDL_CondWait(job_system.new_jobs_available_cond, job_system.new_jobs_available_mutex);
    SDL_UnlockMutex(job_system.new_jobs_available_mutex);

    for (int i = 0; i < ready_to_reset_count; ++i)
      vkResetCommandBuffer(ready_to_reset_commands[i], 0);

    ready_to_reset_count      = recorded_last_frame_count;
    recorded_last_frame_count = just_recorded_count;
    just_recorded_count       = 0;

    VkCommandBuffer* tmp         = ready_to_reset_commands;
    ready_to_reset_commands      = recorded_last_frame_commands;
    recorded_last_frame_commands = just_recorded_commands;
    just_recorded_commands       = tmp;

    int job_idx = SDL_AtomicIncRef(&td.game.js.jobs_taken);

    while (job_idx < job_system.jobs_max)
    {
      ThreadJobData tjd = {just_recorded_commands[just_recorded_count], td.engine, td.game, allocator};
      const Job&    job = job_system.jobs[job_idx];

      ThreadJobStatistic& stat = job_system.profile_data[SDL_AtomicIncRef(&job_system.profile_data_count)];
      stat.threadId            = threadId;
      stat.name                = job.name;

      uint64_t ticks_start = SDL_GetPerformanceCounter();
      int      job_result  = job_system.jobs[job_idx].fcn(tjd);
      stat.duration_sec    = static_cast<float>(SDL_GetPerformanceCounter() - ticks_start) /
                          static_cast<float>(SDL_GetPerformanceFrequency());

      if (-1 != job_result)
        just_recorded_count += 1;
      allocator.reset();

      job_idx = SDL_AtomicIncRef(&td.game.js.jobs_taken);
    }

    if ((SDL_arraysize(job_system.worker_threads) - 1) == SDL_AtomicIncRef(&job_system.threads_finished_work))
      SDL_SemPost(job_system.all_threads_idle_signal);
  }
}

int worker_function_decorator(void* arg)
{
  worker_function(*reinterpret_cast<WorkerThreadData*>(arg));
  return 0;
}

void setup_node_parent_hierarchy(NodeParentHierarchy& dst, const ArrayView<Node>& nodes)
{
  uint8_t* hierarchy = dst.hierarchy;

  for (uint8_t i = 0; i < SDL_arraysize(dst.hierarchy); ++i)
    hierarchy[i] = i;

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
    for (int child_idx : nodes[node_idx].children)
      hierarchy[child_idx] = node_idx;
}

void setup_node_parent_hierarchy(const Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model)
{
  setup_node_parent_hierarchy(ecs.node_parent_hierarchies[entity.node_parent_hierarchy], model.scene_graph.nodes);
}

void propagate_node_renderability_hierarchy(int node_idx, uint64_t& dst, const ArrayView<Node>& nodes)
{
  for (int child_idx : nodes[node_idx].children)
    propagate_node_renderability_hierarchy(child_idx, dst, nodes);
  dst |= (1 << node_idx);
}

void setup_node_renderability_hierarchy(uint64_t& dst, const Scene& scene, const ArrayView<Node>& nodes)
{
  dst = 0;
  for (int scene_node_idx : scene.nodes)
    propagate_node_renderability_hierarchy(scene_node_idx, dst, nodes);
}

void setup_node_renderability_hierarchy(const Entity entity, EntityComponentSystem& ecs,
                                        const gltf::RenderableModel& model)
{
  setup_node_renderability_hierarchy(ecs.node_renderabilities[entity.node_renderabilities], model.scene_graph.scenes[0],
                                     model.scene_graph.nodes);
}

} // namespace

void Game::startup(Engine& engine)
{
  //
  // IMGUI preliminary setup
  //
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

    {
      ArrayView<KeyMapping> mappings = generate_sdl_imgui_keymap(engine.double_ended_stack);
      for (KeyMapping mapping : mappings)
        io.KeyMap[mapping.imgui] = mapping.sdl;
      engine.double_ended_stack.reset_back();
    }

    io.RenderDrawListsFn  = nullptr;
    io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
    io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
    io.ClipboardUserData  = nullptr;

    {
      ArrayView<CursorMapping> mappings = generate_sdl_imgui_cursormap(engine.double_ended_stack);
      for (CursorMapping mapping : mappings)
        debug_gui.mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
      engine.double_ended_stack.reset_back();
    }

    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      debug_gui.vertex_buffer_offsets[i] = engine.gpu_host_visible.allocate(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES);
      debug_gui.index_buffer_offsets[i]  = engine.gpu_host_visible.allocate(DebugGui::INDEX_BUFFER_CAPACITY_BYTES);
    }
  }

  helmet.loadGLB(engine, "../assets/DamagedHelmet.glb");
  helmet_entity.reset();
  helmet_entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
  helmet_entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
  helmet_entity.node_transforms       = ecs.node_transforms_usage.allocate();

  setup_node_parent_hierarchy(helmet_entity, ecs, helmet);
  setup_node_renderability_hierarchy(helmet_entity, ecs, helmet);

  robot.loadGLB(engine, "../assets/su-47.glb");
  robot_entity.reset();
  robot_entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
  robot_entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
  robot_entity.node_transforms       = ecs.node_transforms_usage.allocate();

  setup_node_parent_hierarchy(robot_entity, ecs, robot);
  setup_node_renderability_hierarchy(robot_entity, ecs, robot);

  monster.loadGLB(engine, "../assets/Monster.glb");
  monster_entity.reset();
  monster_entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
  monster_entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
  monster_entity.node_transforms       = ecs.node_transforms_usage.allocate();
  monster_entity.joint_matrices        = ecs.joint_matrices_usage.allocate();

  setup_node_parent_hierarchy(monster_entity, ecs, monster);
  setup_node_renderability_hierarchy(monster_entity, ecs, monster);

  box.loadGLB(engine, "../assets/Box.glb");
  for (Entity& entity : box_entities)
  {
    entity.reset();
    entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
    entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
    entity.node_transforms       = ecs.node_transforms_usage.allocate();
    setup_node_parent_hierarchy(entity, ecs, box);
    setup_node_renderability_hierarchy(entity, ecs, box);
  }

  animatedBox.loadGLB(engine, "../assets/BoxAnimated.glb");
  matrioshka_entity.reset();
  matrioshka_entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
  matrioshka_entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
  matrioshka_entity.node_transforms       = ecs.node_transforms_usage.allocate();

  setup_node_parent_hierarchy(matrioshka_entity, ecs, animatedBox);
  setup_node_renderability_hierarchy(matrioshka_entity, ecs, animatedBox);

  riggedSimple.loadGLB(engine, "../assets/RiggedSimple.glb");
  rigged_simple_entity.reset();
  rigged_simple_entity.node_parent_hierarchy = ecs.node_parent_hierarchies_usage.allocate();
  rigged_simple_entity.node_renderabilities  = ecs.node_renderabilities_usage.allocate();
  rigged_simple_entity.node_transforms       = ecs.node_transforms_usage.allocate();
  rigged_simple_entity.joint_matrices        = ecs.joint_matrices_usage.allocate();

  setup_node_parent_hierarchy(rigged_simple_entity, ecs, riggedSimple);
  setup_node_renderability_hierarchy(rigged_simple_entity, ecs, riggedSimple);

  {
    int cubemap_size[2]     = {512, 512};
    environment_cubemap_idx = generate_cubemap(&engine, this, "../assets/old_industrial_hall.jpg", cubemap_size);
    irradiance_cubemap_idx  = generate_irradiance_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    prefiltered_cubemap_idx = generate_prefiltered_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    brdf_lookup_idx         = generate_brdf_lookup(&engine, cubemap_size[0]);
  }

  green_gui_radar_idx       = engine.load_texture("../assets/radar_small.png");
  lucida_sans_sdf_image_idx = engine.load_texture("../assets/lucida_sans_sdf.png");

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

  for (VkDeviceSize& offset : green_gui_rulers_buffer_offsets)
    offset = engine.gpu_host_visible.allocate(200 * sizeof(vec2));

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
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &radar_texture_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &lucida_sans_sdf_dset);
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

    image.imageView = engine.images.image_views[green_gui_radar_idx];
    write.dstSet    = radar_texture_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);

    image.imageView = engine.images.image_views[lucida_sans_sdf_image_idx];
    write.dstSet    = lucida_sans_sdf_dset;
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

  vec3_set(helmet_translation, 1.0f, 1.0f, 3.0f);
  vec3_set(robot_position, -2.0f, -1.0f, 3.0f);
  vec3_set(rigged_position, -2.0f, 0.0f, 3.0f);

  float extent_width        = static_cast<float>(engine.generic_handles.extent2D.width);
  float extent_height       = static_cast<float>(engine.generic_handles.extent2D.height);
  float aspect_ratio        = extent_width / extent_height;
  float fov                 = to_rad(90.0f);
  float near_clipping_plane = 0.1f;
  float far_clipping_plane  = 1000.0f;
  mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
  projection[1][1] *= -1.0f;

  VrLevelLoadResult result = level_generator_vr(&engine);

  vr_level_vertex_buffer_offset = result.vertex_target_offset;
  vr_level_index_buffer_offset  = result.index_target_offset;
  vr_level_index_type           = result.index_type;
  vr_level_index_count          = result.index_count;

  SDL_memcpy(vr_level_entry, result.entrance_point, sizeof(vec2));
  SDL_memcpy(vr_level_goal, result.target_goal, sizeof(vec2));

  vr_level_entry[0] *= VR_LEVEL_SCALE;
  vr_level_entry[1] *= VR_LEVEL_SCALE;

  vr_level_goal[0] *= 25.0f;
  vr_level_goal[1] *= 25.0f;

  vec3_set(player_position, vr_level_entry[0], 2.0f, vr_level_entry[1]);
  quat_identity(player_orientation);

  vec3_set(player_acceleration, 0.0f, 0.0f, 0.0f);
  vec3_set(player_velocity, 0.0f, 0.0f, 0.0f);

  camera_angle        = static_cast<float>(M_PI / 2);
  camera_updown_angle = -1.2f;

  booster_jet_fuel = 1.0f;

  green_gui_radar_position[0] = -10.2f;
  green_gui_radar_position[1] = -7.3f;
  green_gui_radar_rotation    = -6.0f;

  //
  // billboard vertex data for green gui triangle strip
  //
  {
    struct GreenGuiVertex
    {
      vec2 position;
      vec2 uv;
    };

    GreenGuiVertex vertices[] = {
        {
            .position = {-1.0f, -1.0f},
            .uv       = {0.0f, 0.0f},
        },
        {
            .position = {1.0f, -1.0f},
            .uv       = {1.0f, 0.0f},
        },
        {
            .position = {-1.0f, 1.0f},
            .uv       = {0.0f, 1.0f},
        },
        {
            .position = {1.0f, 1.0f},
            .uv       = {1.0f, 1.0f},
        },
    };

    engine.gpu_static_transfer.used_memory   = 0;
    VkDeviceSize vertices_host_offset        = engine.gpu_static_transfer.allocate(sizeof(vertices));
    green_gui_billboard_vertex_buffer_offset = engine.gpu_static_geometry.allocate(sizeof(vertices));

    {
      ScopedMemoryMap vertices_map(engine.generic_handles.device, engine.gpu_static_transfer.memory,
                                   vertices_host_offset, sizeof(vertices));
      SDL_memcpy(vertices_map.get<void>(), vertices, sizeof(vertices));
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = engine.generic_handles.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(engine.generic_handles.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkBufferCopy copy = {
          .srcOffset = vertices_host_offset,
          .dstOffset = green_gui_billboard_vertex_buffer_offset,
          .size      = sizeof(vertices),
      };

      vkCmdCopyBuffer(cmd, engine.gpu_static_transfer.buffer, engine.gpu_static_geometry.buffer, 1, &copy);
    }

    {
      VkBufferMemoryBarrier barrier = {
          .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .buffer              = engine.gpu_static_geometry.buffer,
          .offset              = green_gui_billboard_vertex_buffer_offset,
          .size                = static_cast<VkDeviceSize>(sizeof(vertices)),
      };

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);
    }

    vkEndCommandBuffer(cmd);

    VkFence data_upload_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(engine.generic_handles.device, &ci, nullptr, &data_upload_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(engine.generic_handles.graphics_queue, 1, &submit, data_upload_fence);
    }

    vkWaitForFences(engine.generic_handles.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine.generic_handles.device, data_upload_fence, nullptr);
    vkFreeCommandBuffers(engine.generic_handles.device, engine.generic_handles.graphics_command_pool, 1, &cmd);
    engine.gpu_static_transfer.used_memory = 0;
  }

  {
    SDL_RWops* ctx              = SDL_RWFromFile("../assets/lucida_sans_sdf.fnt", "r");
    int        fnt_file_size    = static_cast<int>(SDL_RWsize(ctx));
    char*      fnt_file_content = engine.double_ended_stack.allocate_back<char>(fnt_file_size);
    SDL_RWread(ctx, fnt_file_content, sizeof(char), static_cast<size_t>(fnt_file_size));
    SDL_RWclose(ctx);

    auto forward_right_after = [](char* cursor, char target) -> char* {
      while (target != *cursor)
        ++cursor;
      return ++cursor;
    };

    char* cursor = fnt_file_content;
    for (int i = 0; i < 4; ++i)
      cursor = forward_right_after(cursor, '\n');

    for (unsigned i = 0; i < SDL_arraysize(lucida_sans_sdf_chars); ++i)
    {
      uint8_t& id   = lucida_sans_sdf_char_ids[i];
      SdfChar& data = lucida_sans_sdf_chars[i];

      auto read_unsigned = [](char* c) { return SDL_strtoul(c, nullptr, 10); };
      auto read_signed   = [](char* c) { return SDL_strtol(c, nullptr, 10); };

      cursor        = forward_right_after(cursor, '=');
      id            = static_cast<uint8_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.x        = static_cast<uint16_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.y        = static_cast<uint16_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.width    = static_cast<uint8_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.height   = static_cast<uint8_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.xoffset  = static_cast<int8_t>(read_signed(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.yoffset  = static_cast<int8_t>(read_signed(cursor));
      cursor        = forward_right_after(cursor, '=');
      data.xadvance = static_cast<uint8_t>(read_unsigned(cursor));
      cursor        = forward_right_after(cursor, '\n');
    }

    engine.double_ended_stack.reset_back();
  }

  DEBUG_VEC2[0] = 430.0f;
  DEBUG_VEC2[1] = 350.0f;

  js.all_threads_idle_signal  = SDL_CreateSemaphore(0);
  js.new_jobs_available_cond  = SDL_CreateCond();
  js.new_jobs_available_mutex = SDL_CreateMutex();
  js.thread_end_requested     = false;

  for (VkCommandPool& pool : js.worker_pools)
  {
    VkCommandPoolCreateInfo info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = engine.generic_handles.graphics_family_index,
    };
    vkCreateCommandPool(engine.generic_handles.device, &info, nullptr, &pool);
  }

  for (unsigned i = 0; i < SDL_arraysize(js.worker_commands); ++i)
  {
    VkCommandBufferAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = js.worker_pools[i],
        .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = 64 * 3,
    };
    vkAllocateCommandBuffers(engine.generic_handles.device, &info, js.worker_commands[i].commands);
  }

  WorkerThreadData data = {engine, *this};
  for (auto& worker_thread : js.worker_threads)
    worker_thread = SDL_CreateThread(worker_function_decorator, "worker", &data);
  SDL_SemWait(js.all_threads_idle_signal);
  SDL_AtomicSet(&js.threads_finished_work, 0);
}

void Game::teardown(Engine& engine)
{
  vkDeviceWaitIdle(engine.generic_handles.device);

  js.thread_end_requested = true;
  js.jobs_max             = 0;
  SDL_AtomicSet(&js.jobs_taken, 0);

  SDL_CondBroadcast(js.new_jobs_available_cond);

  for (auto& worker_thread : js.worker_threads)
  {
    int retval = 0;
    SDL_WaitThread(worker_thread, &retval);
  }

  for (SDL_Cursor* cursor : debug_gui.mousecursors)
    SDL_FreeCursor(cursor);

  SDL_DestroySemaphore(js.all_threads_idle_signal);
  SDL_DestroyCond(js.new_jobs_available_cond);
  SDL_DestroyMutex(js.new_jobs_available_mutex);

  for (VkCommandPool pool : js.worker_pools)
    vkDestroyCommandPool(engine.generic_handles.device, pool, nullptr);
}

void Game::update(Engine& engine, float time_delta_since_last_frame)
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
          camera_angle += (0.01f * event.motion.xrel);
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
        case SDL_SCANCODE_SPACE:
          player_jump_pressed = (SDL_KEYDOWN == event.type);
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
  ImGui::Begin("Main Panel");

  if (ImGui::CollapsingHeader("Timings"))
  {
    ImGui::PlotHistogram("update times", update_times, SDL_arraysize(update_times), 0, nullptr, 0.0, 0.001,
                         ImVec2(300, 20));
    ImGui::PlotHistogram("render times", render_times, SDL_arraysize(render_times), 0, nullptr, 0.0, 0.03,
                         ImVec2(300, 20));

    ImGui::Text("Average update time: %f", avg(update_times, SDL_arraysize(update_times)));
    ImGui::Text("Average render time: %f", avg(render_times, SDL_arraysize(render_times)));
  }

  if (ImGui::CollapsingHeader("Animations"))
  {
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

    if (ImGui::Button("robot animation"))
      restart_animation(robot, current_time_sec);
    print_animation_stat(robot, current_time_sec);
  }

  if (ImGui::CollapsingHeader("Gameplay features"))
  {
    ImGui::Text("Booster jet fluel");
    ImGui::ProgressBar(booster_jet_fuel);
    ImGui::Text("%d %d | %d %d", lmb_last_cursor_position[0], lmb_last_cursor_position[1],
                lmb_current_cursor_position[0], lmb_current_cursor_position[1]);
  }

  animate_model(animatedBox, current_time_sec);
  animate_model(riggedSimple, current_time_sec);
  animate_model(monster, current_time_sec);
  animate_model(robot, current_time_sec);


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
    player_acceleration[0] += SDL_sinf(camera_angle + (float)M_PI) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle + (float)M_PI) * acceleration;
  }
  else if (player_strafe_right_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle) * acceleration;
  }

  // dirty hack, to be replaced with better code in the future
  const float jump_duration_sec = 0.5f;
  const float jump_height       = 1.0f;
  if (player_jumping)
  {
    if (current_time_sec < (player_jump_start_timestamp_sec + jump_duration_sec))
    {
      const float current_jump_time = (current_time_sec - player_jump_start_timestamp_sec) / jump_duration_sec;
      player_position[1]            = 2.0f - (jump_height * SDL_sinf(current_jump_time * (float)M_PI));
    }
    else
    {
      player_jumping     = false;
      player_position[1] = 2.0f;
    }
  }
  else if (player_jump_pressed)
  {
    player_jumping                  = true;
    player_jump_start_timestamp_sec = current_time_sec;
  }

  float camera_distance = 2.5f;
  float x_camera_offset = SDL_cosf(camera_angle) * camera_distance;
  float y_camera_offset = SDL_sinf(camera_updown_angle) * camera_distance;
  float z_camera_offset = SDL_sinf(camera_angle) * camera_distance;

  camera_position[0] = player_position[0] + x_camera_offset;
  camera_position[1] = y_camera_offset;
  camera_position[2] = player_position[2] - z_camera_offset;

  vec3 center = {player_position[0], 0.0f, player_position[2]};
  vec3 up     = {0.0f, -1.0f, 0.0f};
  mat4x4_look_at(view, camera_position, center, up);

  if (ImGui::CollapsingHeader("Debug and info"))
  {
    ImGui::Text("position:     %.2f %.2f %.2f", player_position[0], player_position[1], player_position[2]);
    ImGui::Text("camera:       %.2f %.2f %.2f", camera_position[0], camera_position[1], camera_position[2]);
    ImGui::Text("acceleration: %.2f %.2f %.2f", player_acceleration[0], player_acceleration[1], player_acceleration[2]);
    ImGui::Text("velocity:     %.2f %.2f %.2f", player_velocity[0], player_velocity[1], player_velocity[2]);
    ImGui::Text("time:         %.4f", current_time_sec);

    ImGui::Text("WASD - movement");
    ImGui::Text("F1 - enable first person view");
    ImGui::Text("F2 - disable first person view");
    ImGui::Text("ESC - exit");

    ImGui::InputFloat2("green_gui_radar_position", green_gui_radar_position);
    ImGui::InputFloat("green_gui_radar_rotation", &green_gui_radar_rotation);
  }

  if (ImGui::Button("quit"))
  {
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
  }

  //
  // Aging and final destruction of scheduled pipelines
  //
  for (int i = 0; i < engine.scheduled_pipelines_destruction_count; ++i)
  {
    ScheduledPipelineDestruction& schedule = engine.scheduled_pipelines_destruction[i];
    schedule.frame_countdown -= 1;
    if (0 == schedule.frame_countdown)
    {
      vkDestroyPipeline(engine.generic_handles.device, schedule.pipeline, nullptr);
      engine.scheduled_pipelines_destruction_count -= 1;

      if (i != engine.scheduled_pipelines_destruction_count)
      {
        schedule = engine.scheduled_pipelines_destruction[engine.scheduled_pipelines_destruction_count + 1];
        i -= 1;
      }
    }
  }

  if (ImGui::CollapsingHeader("Pipeline reload"))
  {
#if 0
    if (ImGui::Button("skybox"))
    {
      pipeline_reload_simple_rendering_skybox_reload(engine);
    }

    if (ImGui::Button("scene3d"))
    {
      pipeline_reload_simple_rendering_scene3d_reload(engine);
    }

    if (ImGui::Button("colored geometry"))
    {
      pipeline_reload_simple_rendering_coloredgeometry_reload(engine);
    }

    if (ImGui::Button("colored geometry skinned"))
    {
      pipeline_reload_simple_rendering_coloredgeometryskinned_reload(engine);
    }

    if (ImGui::Button("imgui"))
    {
      pipeline_reload_simple_rendering_imgui_reload(engine);
    }
#endif

    if (ImGui::Button("green gui sdf"))
    {
      pipeline_reload_simple_rendering_green_gui_sdf_reload(engine);
    }
  }

  if (ImGui::CollapsingHeader("Memory"))
  {
    auto calc_frac = [](VkDeviceSize part, VkDeviceSize max) { return ((float)part / (float)max); };
    ImGui::Text("image memory (%uMB pool)", Engine::Images::MAX_MEMORY_SIZE_MB);
    ImGui::ProgressBar(calc_frac(engine.images.used_memory, Engine::Images::MAX_MEMORY_SIZE));
    ImGui::Text("device-visible memory (%uMB pool)", Engine::GpuStaticGeometry::MAX_MEMORY_SIZE_MB);
    ImGui::ProgressBar(calc_frac(engine.gpu_static_geometry.used_memory, Engine::GpuStaticGeometry::MAX_MEMORY_SIZE));
    ImGui::Text("host-visible memory (%uMB pool)", Engine::GpuHostVisible::MAX_MEMORY_SIZE_MB);
    ImGui::ProgressBar(calc_frac(engine.gpu_static_geometry.used_memory, Engine::GpuStaticGeometry::MAX_MEMORY_SIZE));
    ImGui::Text("UBO memory (%uMB pool)", Engine::UboHostVisible::MAX_MEMORY_SIZE_MB);
    ImGui::ProgressBar(calc_frac(engine.ubo_host_visible.used_memory, Engine::UboHostVisible::MAX_MEMORY_SIZE));
    ImGui::Text("double ended stack memory (%uMB pool)", Engine::DoubleEndedStack::MAX_MEMORY_SIZE_MB);
    ImGui::ProgressBar(calc_frac(static_cast<VkDeviceSize>(engine.double_ended_stack.front),
                                 Engine::DoubleEndedStack::MAX_MEMORY_SIZE));
  }

  if (ImGui::RadioButton("debug flag 1", DEBUG_FLAG_1))
  {
    DEBUG_FLAG_1 = !DEBUG_FLAG_1;
  }

  if (ImGui::RadioButton("debug flag 2", DEBUG_FLAG_2))
  {
    DEBUG_FLAG_2 = !DEBUG_FLAG_2;
  }

  ImGui::InputFloat2("debug vec2", DEBUG_VEC2);

  pbr_light_sources_cache.count = 5;

  auto update_light = [](LightSources& sources, int idx, vec3 position, vec3 color) {
    SDL_memcpy(sources.positions[idx], position, sizeof(vec3));
    SDL_memcpy(sources.colors[idx], color, sizeof(vec3));
  };

  {
    vec3 position = {SDL_sinf(current_time_sec), -0.5f, 3.0f + SDL_cosf(current_time_sec)};
    vec3 color    = {20.0f + (5.0f * SDL_sinf(current_time_sec + 0.4f)), 0.0, 0.0};
    update_light(pbr_light_sources_cache, 0, position, color);
  }

  {
    vec3 position = {0.8f * SDL_cosf(current_time_sec), -0.6f, 3.0f + (0.8f * SDL_sinf(current_time_sec))};
    vec3 color    = {0.0, 20.0, 0.0};
    update_light(pbr_light_sources_cache, 1, position, color);
  }

  {
    vec3 position = {0.8f * SDL_sinf(current_time_sec / 2.0f), -0.3f,
                     3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f))};
    vec3 color    = {0.0, 0.0, 20.0};
    update_light(pbr_light_sources_cache, 2, position, color);
  }

  {
    vec3 position = {SDL_sinf(current_time_sec / 1.2f), -0.1f, 2.5f * SDL_cosf(current_time_sec / 1.2f)};
    vec3 color    = {8.0, 8.0, 8.0};
    update_light(pbr_light_sources_cache, 3, position, color);
  }

  {
    vec3 position = {0.0f, -1.0f, 4.0f};
    vec3 color    = {10.0, 0.0, 10.0};
    update_light(pbr_light_sources_cache, 4, position, color);
  }

  ImGui::End();

  ImGui::Begin("thread profiler");

  if (ImGui::Button("pause"))
  {
    if (not js.is_profiling_paused)
    {
      js.paused_profile_data_count = SDL_AtomicGet(&js.profile_data_count);
      SDL_memcpy(js.paused_profile_data, js.profile_data, js.paused_profile_data_count * sizeof(ThreadJobStatistic));
    }

    js.is_profiling_paused = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("resume"))
  {
    js.is_profiling_paused = false;
  }

  const int profile_data_count =
      js.is_profiling_paused ? js.paused_profile_data_count : SDL_AtomicGet(&js.profile_data_count);
  const ThreadJobStatistic* statistics = js.is_profiling_paused ? js.paused_profile_data : js.profile_data;

  for (int threadId = 0; threadId < static_cast<int>(SDL_arraysize(js.worker_threads)); ++threadId)
  {
    ImGui::Text("worker %d", threadId);
    float on_thread_duration = 0.0f;
    for (int i = 0; i < profile_data_count; ++i)
    {
      const ThreadJobStatistic& stat = statistics[i];
      if (threadId == stat.threadId)
      {
        on_thread_duration += stat.duration_sec;
        ImGui::SameLine();
        ImGui::Button(stat.name, ImVec2(100.0f * 1000.0f * stat.duration_sec, 0));
        if (ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("name: %s\n%.5f sec\n%.2f ms", stat.name, stat.duration_sec, 1000.0f * stat.duration_sec);
        }
      }
    }
    ImGui::Text("total: %.5fms", 1000.0f * on_thread_duration);
    ImGui::Separator();
  }
  ImGui::End();

  // !!!!!!!!!!!!!!! TEST !!!!!!!!!!!!!!!
  // helmet world transform
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

  recalculate_node_transforms(helmet_entity, ecs, helmet, world_transform);
  recalculate_node_transforms(robot_entity, ecs, robot, world_transform);
  recalculate_node_transforms(monster_entity, ecs, monster, world_transform);
  recalculate_skinning_matrices(monster_entity, ecs, monster, world_transform);
  recalculate_node_transforms(matrioshka_entity, ecs, animatedBox, world_transform);
  recalculate_node_transforms(rigged_simple_entity, ecs, riggedSimple, world_transform);
  recalculate_skinning_matrices(rigged_simple_entity, ecs, riggedSimple, world_transform);
  for (Entity& entity : box_entities)
    recalculate_node_transforms(entity, ecs, box, world_transform);
  // !!!!!!!!!!!!!!! TEST !!!!!!!!!!!!!!!
}

void Game::render(Engine& engine)
{
  Engine::SimpleRendering& renderer = engine.simple_rendering;

  vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                        engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
  vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);

  FunctionTimer timer(render_times, SDL_arraysize(render_times));

  update_ubo(engine.generic_handles.device, engine.ubo_host_visible.memory, sizeof(LightSources),
             pbr_dynamic_lights_ubo_offsets[image_index], &pbr_light_sources_cache);

  {
    GenerateGuiLinesCommand cmd = {
        .player_y_location_meters = -(2.0f - player_position[1]),
        .camera_x_pitch_radians   = 0.0f, // to_rad(10) * SDL_sinf(current_time_sec), // simulating future strafe tilts,
        .camera_y_pitch_radians   = camera_updown_angle,
    };

    ArrayView<GuiLine> r = {};
    generate_gui_lines(cmd, nullptr, &r.count);
    r.data = engine.double_ended_stack.allocate_back<GuiLine>(r.count);
    generate_gui_lines(cmd, r.data, &r.count);

    float* pushed_lines_data    = engine.double_ended_stack.allocate_back<float>(4 * r.count);
    int    pushed_lines_counter = 0;

    gui_green_lines_count  = count_lines(r, GuiLine::Color::Green);
    gui_red_lines_count    = count_lines(r, GuiLine::Color::Red);
    gui_yellow_lines_count = count_lines(r, GuiLine::Color::Yellow);

    GuiLine::Color colors_order[] = {
        GuiLine::Color::Green,
        GuiLine::Color::Red,
        GuiLine::Color::Yellow,
    };

    GuiLine::Size sizes_order[] = {
        GuiLine::Size::Big,
        GuiLine::Size::Normal,
        GuiLine::Size::Small,
        GuiLine::Size::Tiny,
    };

    for (GuiLine::Color color : colors_order)
    {
      for (GuiLine::Size size : sizes_order)
      {
        for (const GuiLine& line : r)
        {
          if ((color == line.color) and (size == line.size))
          {
            pushed_lines_data[4 * pushed_lines_counter + 0] = line.a[0];
            pushed_lines_data[4 * pushed_lines_counter + 1] = line.a[1];
            pushed_lines_data[4 * pushed_lines_counter + 2] = line.b[0];
            pushed_lines_data[4 * pushed_lines_counter + 3] = line.b[1];
            pushed_lines_counter++;
          }
        }
      }
    }

    update_ubo(engine.generic_handles.device, engine.gpu_host_visible.memory, r.count * 2 * sizeof(vec2),
               green_gui_rulers_buffer_offsets[image_index], pushed_lines_data);
    engine.double_ended_stack.reset_back();
  }

  ImGui::Render();
  ImDrawData* draw_data = ImGui::GetDrawData();

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  SDL_assert(Game::DebugGui::VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
  SDL_assert(Game::DebugGui::INDEX_BUFFER_CAPACITY_BYTES >= index_size);

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

  js.jobs[0]  = {"skybox", render_skybox_job};
  js.jobs[1]  = {"robot", render_robot_job};
  js.jobs[2]  = {"helmet", render_helmet_job};
  js.jobs[3]  = {"point lights", render_point_light_boxes};
  js.jobs[4]  = {"box", render_matrioshka_box};
  js.jobs[5]  = {"vr scene", render_vr_scene};
  js.jobs[6]  = {"radar", render_radar};
  js.jobs[7]  = {"gui lines", render_robot_gui_lines};
  js.jobs[8]  = {"gui height ruler text", render_height_ruler_text};
  js.jobs[9]  = {"gui tilt ruler text", render_tilt_ruler_text};
  js.jobs[10] = {"hello world", render_hello_world_text};
  js.jobs[11] = {"imgui", render_imgui};
  // js.jobs[11] = render_simple_rigged;
  // js.jobs[12] = render_monster_rigged;
  js.jobs_max = 12;

  SDL_AtomicSet(&js.profile_data_count, 0);
  SDL_CondBroadcast(js.new_jobs_available_cond);
  SDL_SemWait(js.all_threads_idle_signal);
  SDL_AtomicSet(&js.threads_finished_work, 0);

  VkCommandBuffer cmd = engine.simple_rendering.primary_command_buffers[image_index];

  {
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);
  }

  VkClearValue clear_values[] = {
      {.color = {{0.0f, 0.0f, 0.2f, 1.0f}}},
      {.depthStencil = {1.0, 0}},
      {.color = {{0.0f, 0.0f, 0.2f, 1.0f}}},
      {.depthStencil = {1.0, 0}},
  };

  {
    VkRenderPassBeginInfo begin = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = engine.simple_rendering.render_pass,
        .framebuffer     = engine.simple_rendering.framebuffers[image_index],
        .renderArea      = {.extent = engine.generic_handles.extent2D},
        .clearValueCount = SDL_arraysize(clear_values),
        .pClearValues    = clear_values,
    };

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  }

  const int all_secondary_count = SDL_AtomicGet(&js_sink.count);

  for (int i = 0; i < all_secondary_count; ++i)
  {
    const RecordedCommandBuffer& recordere = js_sink.commands[i];
    if (Engine::SimpleRendering::Pass::Skybox == recordere.subpass)
      vkCmdExecuteCommands(cmd, 1, &recordere.command);
  }

  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

  for (int i = 0; i < all_secondary_count; ++i)
  {
    const RecordedCommandBuffer& recordere = js_sink.commands[i];
    if (Engine::SimpleRendering::Pass::Objects3D == recordere.subpass)
      vkCmdExecuteCommands(cmd, 1, &recordere.command);
  }

  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

  for (int i = 0; i < all_secondary_count; ++i)
  {
    const RecordedCommandBuffer& recordere = js_sink.commands[i];
    if (Engine::SimpleRendering::Pass::ImGui == recordere.subpass)
      vkCmdExecuteCommands(cmd, 1, &recordere.command);
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  SDL_AtomicSet(&js_sink.count, 0);
  SDL_AtomicSet(&js.jobs_taken, 0);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submit = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount   = 1,
      .pWaitSemaphores      = &engine.generic_handles.image_available,
      .pWaitDstStageMask    = &wait_stage,
      .commandBufferCount   = 1,
      .pCommandBuffers      = &cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &engine.generic_handles.render_finished,
  };

  vkQueueSubmit(engine.generic_handles.graphics_queue, 1, &submit,
                engine.simple_rendering.submition_fences[image_index]);

  VkPresentInfoKHR present = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &engine.generic_handles.render_finished,
      .swapchainCount     = 1,
      .pSwapchains        = &engine.generic_handles.swapchain,
      .pImageIndices      = &image_index,
  };

  vkQueuePresentKHR(engine.generic_handles.graphics_queue, &present);
}
