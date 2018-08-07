#include "game.hh"
#include "cubemap.hh"
#include "pipelines.hh"
#include "render_jobs.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

constexpr float to_deg(float rad) noexcept
{
  return (180.0f * rad) / float(M_PI);
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

void animate_entity(Entity& entity, EntityComponentSystem& ecs, gltf::RenderableModel& model, float current_time_sec)
{
  if (-1 == entity.animation_start_time)
    return;

  const float      animation_start_time = ecs.animation_start_times[entity.animation_start_time];
  const Animation& animation            = model.scene_graph.animations.data[0];
  const float      animation_time       = current_time_sec - animation_start_time;

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
    ecs.animation_start_times_usage.free(entity.animation_start_time);
    ecs.animation_rotations_usage.free(entity.animation_rotation);
    ecs.animation_translations_usage.free(entity.animation_translation);

    entity.animation_start_time  = -1;
    entity.animation_rotation    = -1;
    entity.animation_translation = -1;

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
        AnimationRotation* rotation_component = nullptr;

        if (-1 == entity.animation_rotation)
        {
          entity.animation_rotation = ecs.animation_rotations_usage.allocate();
          rotation_component        = &ecs.animation_rotations[entity.animation_rotation];
          SDL_memset(rotation_component, 0, sizeof(AnimationRotation));
        }
        else
        {
          rotation_component = &ecs.animation_rotations[entity.animation_rotation];
        }

        rotation_component->applicability |= (1ULL << channel.target_node_idx);

        float* animation_rotation = rotation_component->rotations[channel.target_node_idx];

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[4 * keyframe_lower];
          float* b = &sampler.values[4 * keyframe_upper];
          float* c = animation_rotation;
          lerp(a, b, c, 4, keyframe_uniform_time);
          vec4_norm(c, c);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 4 * keyframe_lower];
          float* b = &sampler.values[3 * 4 * keyframe_upper];
          float* c = animation_rotation;
          hermite_cubic_spline_interpolation(a, b, c, 4, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
          vec4_norm(c, c);
        }
      }
      else if (AnimationChannel::Path::Translation == channel.target_path)
      {
        AnimationTranslation* translation_component = nullptr;

        if (-1 == entity.animation_translation)
        {
          entity.animation_translation = ecs.animation_translations_usage.allocate();
          translation_component        = &ecs.animation_translations[entity.animation_translation];
          SDL_memset(translation_component, 0, sizeof(AnimationTranslation));
        }
        else
        {
          translation_component = &ecs.animation_translations[entity.animation_translation];
        }

        translation_component->applicability |= (1ULL << channel.target_node_idx);

        float* animation_translation = translation_component->animations[channel.target_node_idx];

        if (AnimationSampler::Interpolation::Linear == sampler.interpolation)
        {
          float* a = &sampler.values[3 * keyframe_lower];
          float* b = &sampler.values[3 * keyframe_upper];
          float* c = animation_translation;
          lerp(a, b, c, 3, keyframe_uniform_time);
        }
        else if (AnimationSampler::Interpolation::CubicSpline == sampler.interpolation)
        {
          float* a = &sampler.values[3 * 3 * keyframe_lower];
          float* b = &sampler.values[3 * 3 * keyframe_upper];
          float* c = animation_translation;
          hermite_cubic_spline_interpolation(a, b, c, 3, keyframe_uniform_time,
                                             sampler.time_frame[1] - sampler.time_frame[0]);
        }
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

struct VrLevelLoadResult
{
  float entrance_point[2];
  float target_goal[2];

  VkDeviceSize vertex_target_offset;
  VkDeviceSize index_target_offset;
  int          index_count;
  VkIndexType  index_type;
};

float get_vr_level_height(float x, float y)
{
  return 0.05f * (SDL_cosf(x * 0.5f) + SDL_cosf(y * 0.5f)) - 0.07f;
}

VrLevelLoadResult level_generator_vr(Engine* engine)
{
  float size[]               = {10.0f, 10.0f};
  float resolution[]         = {0.1f, 0.1f};
  int   vertex_counts[]      = {static_cast<int>(size[0] / resolution[0]), static_cast<int>(size[1] / resolution[1])};
  int   total_vertex_count   = vertex_counts[0] * vertex_counts[1];
  int   rectangles_in_row    = vertex_counts[0] - 1;
  int   rectangles_in_column = vertex_counts[1] - 1;
  int   total_index_count    = 6 * (rectangles_in_row) * (rectangles_in_column);

  using IndexType = uint16_t;

  struct Vertex
  {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
  };

  VkDeviceSize host_vertex_offset = engine->gpu_static_transfer.allocate(total_vertex_count * sizeof(Vertex));
  VkDeviceSize host_index_offset  = engine->gpu_static_transfer.allocate(total_index_count * sizeof(IndexType));

  {
    ScopedMemoryMap memory_map(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_vertex_offset,
                               total_vertex_count * sizeof(Vertex));

    Vertex* vertices = memory_map.get<Vertex>();
    float   center[] = {0.5f * size[0], 0.5f * size[1]};

    for (int y = 0; y < vertex_counts[1]; ++y)
    {
      for (int x = 0; x < vertex_counts[0]; ++x)
      {
        Vertex& vtx = vertices[(vertex_counts[0] * y) + x];

        vtx.position[0] = (x * resolution[0]) - center[0];
        vtx.position[1] = get_vr_level_height(x, y);
        vtx.position[2] = (y * resolution[1]) - center[1];

        vtx.texcoord[0] = static_cast<float>(x) / static_cast<float>(vertex_counts[0] / 64);
        vtx.texcoord[1] = static_cast<float>(y) / static_cast<float>(vertex_counts[1] / 64);

        vtx.normal[0] = 0.0f;
        vtx.normal[1] = -1.0f;
        vtx.normal[2] = 0.0f;
      }
    }
  }

  {
    ScopedMemoryMap memory_map(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_index_offset,
                               total_index_count * sizeof(IndexType));

    uint16_t* indices = memory_map.get<uint16_t>();

    for (int y = 0; y < rectangles_in_column; ++y)
    {
      for (int x = 0; x < rectangles_in_row; ++x)
      {
        int x_offset_bottom_line = vertex_counts[0] * y;
        int x_offset_top_line    = vertex_counts[0] * (y + 1);

        indices[0] = static_cast<uint16_t>(x_offset_bottom_line + x + 0);
        indices[1] = static_cast<uint16_t>(x_offset_bottom_line + x + 1);
        indices[2] = static_cast<uint16_t>(x_offset_top_line + x + 0);
        indices[3] = static_cast<uint16_t>(x_offset_top_line + x + 0);
        indices[4] = static_cast<uint16_t>(x_offset_bottom_line + x + 1);
        indices[5] = static_cast<uint16_t>(x_offset_top_line + x + 1);

        indices += 6;
      }
    }
  }

  VkDeviceSize device_vertex_offset = engine->gpu_static_geometry.allocate(total_vertex_count * sizeof(Vertex));
  VkDeviceSize device_index_offset  = engine->gpu_static_geometry.allocate(total_index_count * sizeof(IndexType));

  VrLevelLoadResult result = {
      .entrance_point       = {0.0f, -30.0f},
      .target_goal          = {0.0f, 0.2f},
      .vertex_target_offset = device_vertex_offset,
      .index_target_offset  = device_index_offset,
      .index_count          = total_index_count,
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
            .size      = total_vertex_count * sizeof(Vertex),
        },
        {
            .srcOffset = host_index_offset,
            .dstOffset = device_index_offset,
            .size      = total_index_count * sizeof(IndexType),
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
            .size                = total_vertex_count * sizeof(Vertex),
        },
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = engine->gpu_static_geometry.buffer,
            .offset              = device_index_offset,
            .size                = total_index_count * sizeof(IndexType),
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

} // namespace

// game_generate_gui_lines.cc
void generate_gui_lines(const GenerateGuiLinesCommand& cmd, GuiLine* dst, int* count);

// game_generate_sdl_imgui_mappings.cc
ArrayView<KeyMapping>    generate_sdl_imgui_keymap(Engine::DoubleEndedStack& allocator);
ArrayView<CursorMapping> generate_sdl_imgui_cursormap(Engine::DoubleEndedStack& allocator);

// game_recalculate_node_transforms.cc
void recalculate_node_transforms(Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                 mat4x4 world_transform);
void recalculate_skinning_matrices(Entity entity, EntityComponentSystem& ecs, const gltf::RenderableModel& model,
                                   mat4x4 world_transform);

namespace {

struct WorkerThreadData
{
  Engine& engine;
  Game&   game;
};

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

  SDL_LockMutex(job_system.new_jobs_available_mutex);
  while (not job_system.thread_end_requested)
  {
    //
    // As a proof of concept this signal will always be broadcasted on next render frame.
    //
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

    SDL_LockMutex(job_system.new_jobs_available_mutex);
    if ((SDL_arraysize(job_system.worker_threads) - 1) == SDL_AtomicIncRef(&job_system.threads_finished_work))
      SDL_SemPost(job_system.all_threads_idle_signal);
  }
  SDL_UnlockMutex(job_system.new_jobs_available_mutex);
}

int worker_function_decorator(void* arg)
{
  worker_function(*reinterpret_cast<WorkerThreadData*>(arg));
  return 0;
}

void depth_first_node_parent_hierarchy(uint8_t* hierarchy, const Node* nodes, uint8_t parent_idx, uint8_t node_idx)
{
  for (int child_idx : nodes[node_idx].children)
    depth_first_node_parent_hierarchy(hierarchy, nodes, node_idx, static_cast<uint8_t>(child_idx));
  hierarchy[node_idx] = parent_idx;
}

void setup_node_parent_hierarchy(NodeParentHierarchy& dst, const ArrayView<Node>& nodes)
{
  uint8_t* hierarchy = dst.hierarchy;

  for (uint8_t i = 0; i < SDL_arraysize(dst.hierarchy); ++i)
    hierarchy[i] = i;

  for (uint8_t node_idx = 0; node_idx < nodes.count; ++node_idx)
    for (int child_idx : nodes[node_idx].children)
      depth_first_node_parent_hierarchy(hierarchy, nodes.data, node_idx, static_cast<uint8_t>(child_idx));
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

float ease_in_out_quart(float t)
{
  if (t < 0.5)
  {
    t *= t;
    return 8 * t * t;
  }
  else
  {
    t = (t - 1.0f) * t;
    return 1 - 8 * t * t;
  }
}

} // namespace

void WeaponSelection::init()
{
  src                   = 1;
  dst                   = 1;
  switch_animation      = false;
  switch_animation_time = 0.0f;
}

void WeaponSelection::select(int new_dst)
{
  if ((not switch_animation) and (new_dst != src))
  {
    dst                   = new_dst;
    switch_animation      = true;
    switch_animation_time = 0.0f;
  }
}

void WeaponSelection::animate(float step)
{
  if (not switch_animation)
    return;

  switch_animation_time += step;
  if (switch_animation_time > 1.0f)
  {
    switch_animation_time = 1.0f;
    switch_animation      = false;
    src                   = dst;
  }
}

void WeaponSelection::calculate(float transparencies[3])
{
  const float highlighted_value = 1.0f;
  const float dimmed_value      = 0.4f;

  if (not switch_animation)
  {
    for (int i = 0; i < 3; ++i)
      transparencies[i] = (i == dst) ? highlighted_value : dimmed_value;
  }
  else
  {

    for (int i = 0; i < 3; ++i)
    {
      if (i == src)
      {
        transparencies[i] = 1.0f - (0.6f * ease_in_out_quart(switch_animation_time));
      }
      else if (i == dst)
      {
        transparencies[i] = 0.4f + (0.6f * ease_in_out_quart(switch_animation_time));
      }
      else
      {
        transparencies[i] = 0.4f;
      }
    }
  }
}

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
    environment_cubemap_idx = generate_cubemap(&engine, this, "../assets/mono_lake.jpg", cubemap_size);
    irradiance_cubemap_idx  = generate_irradiance_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    prefiltered_cubemap_idx = generate_prefiltered_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    brdf_lookup_idx         = generate_brdf_lookup(&engine, cubemap_size[0]);
  }

  lucida_sans_sdf_image_idx = engine.load_texture("../assets/lucida_sans_sdf.png");

  // Sand PBR for environment
  sand_albedo_idx             = engine.load_texture("../assets/pbr_sand/sand_albedo.jpg");
  sand_ambient_occlusion_idx  = engine.load_texture("../assets/pbr_sand/sand_ambient_occlusion.jpg");
  sand_metallic_roughness_idx = engine.load_texture("../assets/pbr_sand/sand_metallic_roughness.jpg");
  sand_normal_idx             = engine.load_texture("../assets/pbr_sand/sand_normal.jpg");
  sand_emissive_idx           = engine.load_texture("../assets/pbr_sand/sand_emissive.jpg");

  water_normal_idx = engine.load_texture("../assets/pbr_water/normal_map.jpg");

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
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &sandy_level_pbr_material_dset);
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

    Material sand_material = {
        .albedo_texture_idx          = sand_albedo_idx,
        .metal_roughness_texture_idx = sand_metallic_roughness_idx,
        .emissive_texture_idx        = sand_emissive_idx,
        .AO_texture_idx              = sand_ambient_occlusion_idx,
        .normal_texture_idx          = sand_normal_idx,
    };
    fill_infos(sand_material, engine.images.image_views, images);
    update.dstSet = sandy_level_pbr_material_dset;
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
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &lucida_sans_sdf_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &pbr_water_material_dset);
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

    image.imageView = engine.images.image_views[lucida_sans_sdf_image_idx];
    write.dstSet    = lucida_sans_sdf_dset;
    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &write, 0, nullptr);

    image.imageView = engine.images.image_views[water_normal_idx];
    write.dstSet    = pbr_water_material_dset;
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

  vec3_set(robot_position, -2.0f, 3.0f, 3.0f);
  vec3_set(rigged_position, -2.0f, 3.0f, 3.0f);

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

  // vr_level_entry[0] *= VR_LEVEL_SCALE;
  // vr_level_entry[1] *= VR_LEVEL_SCALE;

  vr_level_goal[0] *= 25.0f;
  vr_level_goal[1] *= 25.0f;

  vec3_set(player_position, vr_level_entry[0], 0.0f, vr_level_entry[1]);

  vec3_set(player_acceleration, 0.0f, 0.0f, 0.0f);
  vec3_set(player_velocity, 0.0f, 0.0f, 0.0f);

  camera_angle        = static_cast<float>(M_PI / 2);
  camera_updown_angle = -1.2f;

  booster_jet_fuel = 1.0f;

  green_gui_radar_position[0] = -10.2f;
  green_gui_radar_position[1] = -7.3f;
  green_gui_radar_rotation    = -6.0f;

  //
  // billboard vertex data (triangle strip topology)
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

    struct ColoredGeometryVertex
    {
      vec3 position;
      vec3 normal;
      vec2 tex_coord;
    };

    ColoredGeometryVertex cg_vertices[] = {
        {
            .position  = {-1.0f, -1.0f, 0.0f},
            .normal    = {0.0f, 0.0f, 1.0f},
            .tex_coord = {0.0f, 0.0f},
        },
        {
            .position  = {1.0f, -1.0f, 0.0f},
            .normal    = {0.0f, 0.0f, 1.0f},
            .tex_coord = {1.0f, 0.0f},
        },
        {
            .position  = {-1.0f, 1.0f, 0.0f},
            .normal    = {0.0f, 0.0f, 1.0f},
            .tex_coord = {0.0f, 1.0f},
        },
        {
            .position  = {1.0f, 1.0f, 0.0f},
            .normal    = {0.0f, 0.0f, 1.0f},
            .tex_coord = {1.0f, 1.0f},
        },
    };

    engine.gpu_static_transfer.used_memory = 0;

    VkDeviceSize vertices_host_offset        = engine.gpu_static_transfer.allocate(sizeof(vertices));
    green_gui_billboard_vertex_buffer_offset = engine.gpu_static_geometry.allocate(sizeof(vertices));
    {
      ScopedMemoryMap vertices_map(engine.generic_handles.device, engine.gpu_static_transfer.memory,
                                   vertices_host_offset, sizeof(vertices));
      SDL_memcpy(vertices_map.get<void>(), vertices, sizeof(vertices));
    }

    VkDeviceSize cg_vertices_host_offset   = engine.gpu_static_transfer.allocate(sizeof(cg_vertices));
    regular_billboard_vertex_buffer_offset = engine.gpu_static_geometry.allocate(sizeof(cg_vertices));
    {
      ScopedMemoryMap vertices_map(engine.generic_handles.device, engine.gpu_static_transfer.memory,
                                   cg_vertices_host_offset, sizeof(cg_vertices));
      SDL_memcpy(vertices_map.get<void>(), cg_vertices, sizeof(cg_vertices));
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
      VkBufferCopy copies[] = {
          {
              .srcOffset = vertices_host_offset,
              .dstOffset = green_gui_billboard_vertex_buffer_offset,
              .size      = sizeof(vertices),
          },
          {
              .srcOffset = cg_vertices_host_offset,
              .dstOffset = regular_billboard_vertex_buffer_offset,
              .size      = sizeof(cg_vertices),
          },
      };

      vkCmdCopyBuffer(cmd, engine.gpu_static_transfer.buffer, engine.gpu_static_geometry.buffer, SDL_arraysize(copies),
                      copies);
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

  DEBUG_VEC2[0] = 122.0f;
  DEBUG_VEC2[1] = 69.5f;

  DEBUG_VEC2_ADDITIONAL[0] = 240.0f;
  DEBUG_VEC2_ADDITIONAL[1] = 43.0f;

  radar_scale = 0.75f;

  diagnostic_meas_scale = 1.0f;

  js.all_threads_idle_signal  = SDL_CreateSemaphore(0);
  js.new_jobs_available_cond  = SDL_CreateCond();
  js.new_jobs_available_mutex = SDL_CreateMutex();
  js.thread_end_requested     = false;

  for (WeaponSelection& sel : weapon_selections)
    sel.init();

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

void Game::update(Engine& engine, float time_delta_since_last_frame_ms)
{
  FunctionTimer timer(update_times, SDL_arraysize(update_times));

  ImGuiIO& io             = ImGui::GetIO();
  bool     quit_requested = false;

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

          if (camera_angle > 2 * float(M_PI))
          {
            camera_angle -= 2 * float(M_PI);
          }
          else if (camera_angle < 0.0f)
          {
            camera_angle += 2 * float(M_PI);
          }

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
        case SDL_SCANCODE_1:
          weapon_selections[0].select(0);
          break;
        case SDL_SCANCODE_2:
          weapon_selections[0].select(1);
          break;
        case SDL_SCANCODE_3:
          weapon_selections[0].select(2);
          break;
        case SDL_SCANCODE_4:
          weapon_selections[1].select(0);
          break;
        case SDL_SCANCODE_5:
          weapon_selections[1].select(1);
          break;
        case SDL_SCANCODE_6:
          weapon_selections[1].select(2);
          break;
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

  for (WeaponSelection& sel : weapon_selections)
    sel.animate(0.008f * time_delta_since_last_frame_ms);

  ImGui::NewFrame();
  ImGui::Begin("Main Panel");

  if (ImGui::CollapsingHeader("Timings"))
  {
    ImGui::PlotHistogram("update times", update_times, SDL_arraysize(update_times), 0, nullptr, 0.0, 0.005,
                         ImVec2(300, 20));
    ImGui::PlotHistogram("render times", render_times, SDL_arraysize(render_times), 0, nullptr, 0.0, 0.005,
                         ImVec2(300, 20));

    ImGui::Text("Average update time:            %.2fms", 1000.0f * avg(update_times, SDL_arraysize(update_times)));
    ImGui::Text("Average render time:            %.2fms", 1000.0f * avg(render_times, SDL_arraysize(render_times)));
  }

  if (ImGui::CollapsingHeader("Animations"))
  {
    if (ImGui::Button("restart cube animation"))
    {
      if (-1 == matrioshka_entity.animation_start_time)
      {
        matrioshka_entity.animation_start_time                            = ecs.animation_start_times_usage.allocate();
        ecs.animation_start_times[matrioshka_entity.animation_start_time] = current_time_sec;
      }
    }

    if (ImGui::Button("restart rigged animation"))
    {
      if (-1 == rigged_simple_entity.animation_start_time)
      {
        rigged_simple_entity.animation_start_time = ecs.animation_start_times_usage.allocate();
        ecs.animation_start_times[rigged_simple_entity.animation_start_time] = current_time_sec;
      }
    }

    if (ImGui::Button("monster animation"))
    {
      if (-1 == monster_entity.animation_start_time)
      {
        monster_entity.animation_start_time                            = ecs.animation_start_times_usage.allocate();
        ecs.animation_start_times[monster_entity.animation_start_time] = current_time_sec;
      }
    }
  }

  if (ImGui::CollapsingHeader("Gameplay features"))
  {
    ImGui::Text("Booster jet fluel");
    ImGui::ProgressBar(booster_jet_fuel);
    ImGui::Text("%d %d | %d %d", lmb_last_cursor_position[0], lmb_last_cursor_position[1],
                lmb_current_cursor_position[0], lmb_current_cursor_position[1]);
  }

  for (int i = 0; i < 3; ++i)
  {
    player_position[i] += player_velocity[i] * time_delta_since_last_frame_ms;
    const float friction = 0.2f;
    float       drag     = friction * player_velocity[i];
    player_velocity[i] += player_acceleration[i] * time_delta_since_last_frame_ms;

    if (player_velocity[i])
    {
      player_velocity[i] -= drag;
    }
    else
    {
      player_velocity[i] += drag;
    }

    const float max_speed = 3.0f;
    player_velocity[i]    = clamp(player_velocity[i], -max_speed, max_speed);

    player_acceleration[i] = 0.0f;
  }

  float acceleration = 0.0002f;
  if (player_booster_activated and
      (player_forward_pressed or player_back_pressed or player_strafe_left_pressed or player_strafe_right_pressed))
  {
    if (booster_jet_fuel > 0.0f)
    {
      // booster_jet_fuel -= 0.001f;
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
  const float jump_duration_sec = 1.5f;
  const float jump_height       = 5.0f;
  if (player_jumping)
  {
    if (current_time_sec < (player_jump_start_timestamp_sec + jump_duration_sec))
    {
      const float current_jump_time = (current_time_sec - player_jump_start_timestamp_sec) / jump_duration_sec;

      // For now this is hardcoded to fit height sampling function.
      // @todo: Refactor to something readable
      player_position[1] =
          100.0f * get_vr_level_height(-0.1f * player_position[0] + 0.25f, -0.1f * player_position[2] + 0.25f) + 2.5f;
      player_position[1] -= (jump_height * SDL_sinf(current_jump_time * (float)M_PI));
    }
    else
    {
      player_jumping = false;
    }
  }
  else
  {
    // For now this is hardcoded to fit height sampling function.
    // @todo: Refactor to something readable
    player_position[1] =
        100.0f * get_vr_level_height(-0.1f * player_position[0] + 0.25f, -0.1f * player_position[2] + 0.25f) + 2.5f;

    if (player_jump_pressed)
    {
      player_jumping                  = true;
      player_jump_start_timestamp_sec = current_time_sec;
    }
  }

  const float camera_distance = 3.0f;
  float       x_camera_offset = SDL_cosf(camera_angle) * camera_distance;
  float       y_camera_offset = SDL_sinf(clamp(camera_updown_angle, -1.5f, 1.5f)) * camera_distance;
  float       z_camera_offset = SDL_sinf(camera_angle) * camera_distance;

  camera_position[0] = player_position[0] + x_camera_offset;
  camera_position[1] = player_position[1] + y_camera_offset - 1.5f;
  camera_position[2] = player_position[2] - z_camera_offset;

  vec3 center = {player_position[0], player_position[1] - 1.5f, player_position[2]};
  vec3 up     = {0.0f, -1.0f, 0.0f};
  mat4x4_look_at(view, camera_position, center, up);

  if (ImGui::CollapsingHeader("Debug and info"))
  {
    ImGui::Text("camera offsets: %.2f %.2f %.2f", x_camera_offset, y_camera_offset, z_camera_offset);
    ImGui::Text("camera angles: %.2f %.2f", camera_angle, camera_updown_angle);

    ImGui::Text("position:     %.2f %.2f %.2f", player_position[0], player_position[1], player_position[2]);
    ImGui::Text("camera:       %.2f %.2f %.2f", camera_position[0], camera_position[1], camera_position[2]);
    ImGui::Text("acceleration: %.2f %.2f %.2f", player_acceleration[0], player_acceleration[1], player_acceleration[2]);
    ImGui::Text("velocity:     %.2f %.2f %.2f", player_velocity[0], player_velocity[1], player_velocity[2]);
    ImGui::Text("time:         %.4f", current_time_sec);
    ImGui::Text("camera angle: %.2f", to_deg(camera_angle));

    ImGui::Text("WASD - movement");
    ImGui::Text("F1 - enable first person view");
    ImGui::Text("F2 - disable first person view");
    ImGui::Text("ESC - exit");

    ImGui::InputFloat2("green_gui_radar_position", green_gui_radar_position);
    ImGui::InputFloat("green_gui_radar_rotation", &green_gui_radar_rotation);
    ImGui::InputFloat("radar scale", &radar_scale);
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
    if (ImGui::Button("water shaders"))
      pipeline_reload_simple_rendering_pbr_water_reload(engine);

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
  ImGui::InputFloat2("debug vec2 additional", DEBUG_VEC2_ADDITIONAL);

  pbr_light_sources_cache.count = 5;

  auto update_light = [](LightSources& sources, int idx, vec3 position, vec3 color) {
    SDL_memcpy(sources.positions[idx], position, sizeof(vec3));
    SDL_memcpy(sources.colors[idx], color, sizeof(vec3));
  };

  {
    vec3 position = {SDL_sinf(current_time_sec), 3.5f, 3.0f + SDL_cosf(current_time_sec)};
    vec3 color    = {20.0f + (5.0f * SDL_sinf(current_time_sec + 0.4f)), 0.0, 0.0};
    update_light(pbr_light_sources_cache, 0, position, color);
  }

  {
    vec3 position = {0.8f * SDL_cosf(current_time_sec), 3.6f, 3.0f + (0.8f * SDL_sinf(current_time_sec))};
    vec3 color    = {0.0, 20.0, 0.0};
    update_light(pbr_light_sources_cache, 1, position, color);
  }

  {
    vec3 position = {0.8f * SDL_sinf(current_time_sec / 2.0f), 3.3f, 3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f))};
    vec3 color    = {0.0, 0.0, 20.0};
    update_light(pbr_light_sources_cache, 2, position, color);
  }

  {
    vec3 position = {SDL_sinf(current_time_sec / 1.2f), 3.1f, 2.5f * SDL_cosf(current_time_sec / 1.2f)};
    vec3 color    = {8.0, 8.0, 8.0};
    update_light(pbr_light_sources_cache, 3, position, color);
  }

  {
    vec3 position = {0.0f, 3.0f, 4.0f};
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

  ImGui::SameLine();
  ImGui::SliderFloat("measurements scale", &diagnostic_meas_scale, 1.0f, 100.0f);

  const int profile_data_count =
      js.is_profiling_paused ? js.paused_profile_data_count : SDL_AtomicGet(&js.profile_data_count);
  const ThreadJobStatistic* statistics = js.is_profiling_paused ? js.paused_profile_data : js.profile_data;

  for (int threadId = 0; threadId < static_cast<int>(SDL_arraysize(js.worker_threads)); ++threadId)
  {
    ImGui::Text("worker %d", threadId);
    float on_thread_duration     = 0.0f;
    bool  first_element_indented = false;

    for (int i = 0; i < profile_data_count; ++i)
    {
      const ThreadJobStatistic& stat = statistics[i];
      if (threadId == stat.threadId)
      {
        const float scaled_duration = diagnostic_meas_scale * stat.duration_sec;
        on_thread_duration += stat.duration_sec;

        if (not first_element_indented)
        {
          ImGui::SameLine();
          first_element_indented = true;
        }
        else
        {
          ImGui::SameLine(0.0f, 0.1f);
        }

        // IMGUI_API bool          ColorButton(const char* desc_id, const ImVec4& col, ImGuiColorEditFlags flags = 0,
        // ImVec2 size = ImVec2(0,0));  // display a colored square/button, hover for details, return true when pressed.
        ImGui::ColorButton(stat.name, ImVec4((float)i / (float)profile_data_count, 0.1f, 0.1, 1.0f), 0,
                           ImVec2(100.0f * 1000.0f * scaled_duration, 0));
        if (ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("name: %s\n%.8f sec\n%.2f ms", stat.name, stat.duration_sec, 1000.0f * stat.duration_sec);
        }
      }
    }
    ImGui::Text("total: %.5fms", 1000.0f * on_thread_duration);
    ImGui::Separator();
  }
  ImGui::End();

  mat4x4 world_transform = {};

  {
    Quaternion orientation;
    orientation.rotateX(to_rad(180.0));

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, vr_level_goal[0], 3.0f, vr_level_goal[1]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 1.6f, 1.6f, 1.6f);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  recalculate_node_transforms(helmet_entity, ecs, helmet, world_transform);

  {
    Quaternion orientation;

    {
      Quaternion standing_pose;
      standing_pose.rotateX(to_rad(180.0));

      Quaternion rotate_back;
      rotate_back.rotateY(player_position[0] < camera_position[0] ? to_rad(180.0f) : to_rad(0.0f));

      float      x_delta = player_position[0] - camera_position[0];
      float      z_delta = player_position[2] - camera_position[2];
      Quaternion camera;
      camera.rotateY(static_cast<float>(SDL_atan(z_delta / x_delta)));

      orientation = standing_pose * rotate_back * camera;
    }

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, player_position[0], player_position[1] - 1.0f, player_position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.5f, 0.5f, 0.5f);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  recalculate_node_transforms(robot_entity, ecs, robot, world_transform);

  {
    Quaternion orientation;
    orientation.rotateX(to_rad(45.0f));

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, -2.0f, 5.5f, 0.5f);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    float factor = 0.025f;
    mat4x4_scale_aniso(scale_matrix, scale_matrix, factor, factor, factor);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, rotation_matrix, translation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  animate_entity(monster_entity, ecs, monster, current_time_sec);
  recalculate_node_transforms(monster_entity, ecs, monster, world_transform);
  recalculate_skinning_matrices(monster_entity, ecs, monster, world_transform);

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
    mat4x4_mul(world_transform, tmp, scale_matrix);
  }

  animate_entity(rigged_simple_entity, ecs, riggedSimple, current_time_sec);
  recalculate_node_transforms(rigged_simple_entity, ecs, riggedSimple, world_transform);
  recalculate_skinning_matrices(rigged_simple_entity, ecs, riggedSimple, world_transform);

  for (int i = 0; i < pbr_light_sources_cache.count; ++i)
  {
    Quaternion orientation = Quaternion().rotateZ(to_rad(100.0f * current_time_sec)) *
                             Quaternion().rotateY(to_rad(280.0f * current_time_sec)) *
                             Quaternion().rotateX(to_rad(60.0f * current_time_sec));

    float* position = pbr_light_sources_cache.positions[i];

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, position[0], position[1], position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());

    mat4x4 scale_matrix = {};
    mat4x4_identity(scale_matrix);
    mat4x4_scale_aniso(scale_matrix, scale_matrix, 0.05f, 0.05f, 0.05f);

    mat4x4 tmp = {};
    mat4x4_mul(tmp, translation_matrix, rotation_matrix);
    mat4x4_mul(world_transform, tmp, scale_matrix);

    recalculate_node_transforms(box_entities[i], ecs, box, world_transform);
  }

  {
    Quaternion orientation = Quaternion().rotateZ(to_rad(90.0f * current_time_sec / 90.0f)) *
                             Quaternion().rotateY(to_rad(140.0f * current_time_sec / 30.0f)) *
                             Quaternion().rotateX(to_rad(90.0f * current_time_sec / 20.0f));

    mat4x4 translation_matrix = {};
    mat4x4_translate(translation_matrix, robot_position[0], robot_position[1], robot_position[2]);

    mat4x4 rotation_matrix = {};
    mat4x4_from_quat(rotation_matrix, orientation.data());
    mat4x4_mul(world_transform, translation_matrix, rotation_matrix);
  }

  animate_entity(matrioshka_entity, ecs, animatedBox, current_time_sec);
  recalculate_node_transforms(matrioshka_entity, ecs, animatedBox, world_transform);

  ImGui::Render();
}

void Game::render(Engine& engine)
{
  Engine::SimpleRendering& renderer = engine.simple_rendering;

  {
    vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                          engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
    vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
    vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);
  }

  FunctionTimer timer(render_times, SDL_arraysize(render_times));

  js.jobs_max            = 0;
  js.jobs[js.jobs_max++] = {"skybox", render::skybox_job};
  js.jobs[js.jobs_max++] = {"robot", render::robot_job};
  js.jobs[js.jobs_max++] = {"helmet", render::helmet_job};
  js.jobs[js.jobs_max++] = {"point lights", render::point_light_boxes};
  js.jobs[js.jobs_max++] = {"box", render::matrioshka_box};
  js.jobs[js.jobs_max++] = {"vr scene", render::vr_scene};
  js.jobs[js.jobs_max++] = {"radar", render::radar};
  js.jobs[js.jobs_max++] = {"gui lines", render::robot_gui_lines};
  js.jobs[js.jobs_max++] = {"gui height ruler text", render::height_ruler_text};
  js.jobs[js.jobs_max++] = {"gui tilt ruler text", render::tilt_ruler_text};
  js.jobs[js.jobs_max++] = {"imgui", render::imgui};
  js.jobs[js.jobs_max++] = {"simple rigged", render::simple_rigged};
  js.jobs[js.jobs_max++] = {"monster", render::monster_rigged};
  js.jobs[js.jobs_max++] = {"speed meter", render::robot_gui_speed_meter_text};
  js.jobs[js.jobs_max++] = {"speed meter triangle", render::robot_gui_speed_meter_triangle};
  js.jobs[js.jobs_max++] = {"compass text", render::compass_text};
  js.jobs[js.jobs_max++] = {"radar dots", render::radar_dots};
  js.jobs[js.jobs_max++] = {"weapon selectors - left", render::weapon_selectors_left};
  js.jobs[js.jobs_max++] = {"weapon selectors - right", render::weapon_selectors_right};
  js.jobs[js.jobs_max++] = {"water", render::water};

  SDL_AtomicSet(&js.profile_data_count, 0);
  SDL_LockMutex(js.new_jobs_available_mutex);
  SDL_CondBroadcast(js.new_jobs_available_cond);
  SDL_UnlockMutex(js.new_jobs_available_mutex);

  update_ubo(engine.generic_handles.device, engine.ubo_host_visible.memory, sizeof(LightSources),
             pbr_dynamic_lights_ubo_offsets[image_index], &pbr_light_sources_cache);

  //
  // rigged simple skinning matrices
  //
  update_ubo(engine.generic_handles.device, engine.ubo_host_visible.memory,
             SDL_arraysize(ecs.joint_matrices[0].joints) * sizeof(mat4x4),
             rig_skinning_matrices_ubo_offsets[image_index],
             ecs.joint_matrices[rigged_simple_entity.joint_matrices].joints);

  //
  // monster skinning matrices
  //
  update_ubo(engine.generic_handles.device, engine.ubo_host_visible.memory,
             SDL_arraysize(ecs.joint_matrices[0].joints) * sizeof(mat4x4),
             monster_skinning_matrices_ubo_offsets[image_index],
             ecs.joint_matrices[monster_entity.joint_matrices].joints);

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
    if (Engine::SimpleRendering::Pass::RobotGui == recordere.subpass)
      vkCmdExecuteCommands(cmd, 1, &recordere.command);
  }

  vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

  for (int i = 0; i < all_secondary_count; ++i)
  {
    const RecordedCommandBuffer& recordere = js_sink.commands[i];
    if (Engine::SimpleRendering::Pass::RadarDots == recordere.subpass)
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
