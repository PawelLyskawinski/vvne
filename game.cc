#include "game.hh"
#include "cubemap.hh"
#include "render_jobs.hh"
#include "update_jobs.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>

namespace {

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

float avg(const float* values, int n)
{
  float sum = 0.0f;
  for (int i = 0; i < n; ++i)
    sum += values[i];
  sum /= n;
  return sum;
}

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

float get_vr_level_height(float x, float y) { return 0.05f * (SDL_cosf(x * 0.5f) + SDL_cosf(y * 0.5f)) - 0.07f; }

void update_ubo(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, VkDeviceSize offset, void* src)
{
  void* data = nullptr;
  vkMapMemory(device, memory, offset, size, 0, &data);
  SDL_memcpy(data, src, size);
  vkUnmapMemory(device, memory);
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

  VkDeviceSize host_vertex_offset = 0;
  VkDeviceSize host_index_offset  = 0;

  {
    GpuMemoryBlock& block = engine->memory_blocks.host_visible_transfer_source;

    host_vertex_offset = block.stack_pointer;
    block.stack_pointer += align(total_vertex_count * sizeof(Vertex), block.alignment);

    host_index_offset = block.stack_pointer;
    block.stack_pointer += align(total_index_count * sizeof(IndexType), block.alignment);

    Vertex* vertices = nullptr;
    vkMapMemory(engine->device, block.memory, host_vertex_offset, total_vertex_count * sizeof(Vertex), 0,
                reinterpret_cast<void**>(&vertices));

    const float center[] = {0.5f * size[0], 0.5f * size[1]};

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

    vkUnmapMemory(engine->device, block.memory);

    uint16_t* indices = nullptr;
    vkMapMemory(engine->device, block.memory, host_index_offset, total_index_count * sizeof(IndexType), 0,
                reinterpret_cast<void**>(&indices));

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

    vkUnmapMemory(engine->device, block.memory);
  }

  VkDeviceSize device_vertex_offset = 0;
  VkDeviceSize device_index_offset  = 0;

  {
    GpuMemoryBlock& block = engine->memory_blocks.device_local;

    device_vertex_offset = block.stack_pointer;
    block.stack_pointer += align(total_vertex_count * sizeof(Vertex), block.alignment);

    device_index_offset = block.stack_pointer;
    block.stack_pointer += align(total_index_count * sizeof(IndexType), block.alignment);
  }

  VrLevelLoadResult result = {
      .entrance_point       = {0.0f, -1.0f},
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
        .commandPool        = engine->graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(engine->device, &allocate, &cmd);

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

    vkCmdCopyBuffer(cmd, engine->gpu_host_visible_transfer_source_memory_buffer, engine->gpu_device_local_memory_buffer,
                    SDL_arraysize(copies), copies);

    VkBufferMemoryBarrier barriers[] = {
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = engine->gpu_device_local_memory_buffer,
            .offset              = device_vertex_offset,
            .size                = total_vertex_count * sizeof(Vertex),
        },
        {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = engine->gpu_device_local_memory_buffer,
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
      vkCreateFence(engine->device, &ci, nullptr, &data_upload_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(engine->graphics_queue, 1, &submit, data_upload_fence);
    }

    vkWaitForFences(engine->device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine->device, data_upload_fence, nullptr);
    vkFreeCommandBuffers(engine->device, engine->graphics_command_pool, 1, &cmd);
  }

  engine->memory_blocks.host_visible_transfer_source.stack_pointer = 0;
  engine->dirty_stack.reset();

  return result;
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

  Stack allocator{};
  allocator.setup(1024);

  SDL_LockMutex(job_system.new_jobs_available_mutex);
  while (not job_system.thread_end_requested)
  {
    SDL_CondWait(job_system.new_jobs_available_cond, job_system.new_jobs_available_mutex);
    SDL_UnlockMutex(job_system.new_jobs_available_mutex);

    int job_idx = SDL_AtomicIncRef(&td.game.js.jobs_taken);

    while (job_idx < job_system.jobs_max)
    {
      ThreadJobData tjd = {
          .thread_id = threadId,
          .engine    = td.engine,
          .game      = td.game,
          .allocator = allocator,
      };

      uint64_t ticks_start = SDL_GetPerformanceCounter();
      job_system.jobs[job_idx](tjd);

      job_system.push_profile_data(threadId,
                                   static_cast<float>(SDL_GetPerformanceCounter() - ticks_start) /
                                       static_cast<float>(SDL_GetPerformanceFrequency()),
                                   job_system.job_names[job_idx]);

      allocator.reset();
      job_idx = SDL_AtomicIncRef(&td.game.js.jobs_taken);
    }

    SDL_LockMutex(job_system.new_jobs_available_mutex);
    if ((SDL_arraysize(job_system.worker_threads) - 1) == SDL_AtomicIncRef(&job_system.threads_finished_work))
      SDL_SemPost(job_system.all_threads_idle_signal);
  }
  SDL_UnlockMutex(job_system.new_jobs_available_mutex);
  allocator.teardown();
}

int worker_function_decorator(void* arg)
{
  worker_function(*reinterpret_cast<WorkerThreadData*>(arg));
  return 0;
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
    SDL_Surface* surface   = SDL_CreateRGBSurfaceWithFormatFrom(guifont_pixels, guifont_w, guifont_h, 32, 4 * guifont_w,
                                                              SDL_PIXELFORMAT_RGBA8888);
    debug_gui.font_texture = engine.load_texture(surface);
    SDL_FreeSurface(surface);

    {
      struct KeyMapping
      {
        ImGuiKey_    imgui;
        SDL_Scancode sdl;
      };

      KeyMapping mappings[] = {
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
          {ImGuiKey_Z, SDL_SCANCODE_Z},
      };

      for (KeyMapping mapping : mappings)
        io.KeyMap[mapping.imgui] = mapping.sdl;
    }

    io.RenderDrawListsFn  = nullptr;
    io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
    io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
    io.ClipboardUserData  = nullptr;

    {
      struct CursorMapping
      {
        ImGuiMouseCursor_ imgui;
        SDL_SystemCursor  sdl;
      };

      CursorMapping mappings[] = {
          {ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW},
          {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM},
          {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL},
          {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS},
          {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE},
          {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW},
          {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE},
      };

      for (CursorMapping mapping : mappings)
        debug_gui.mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
    }

    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      GpuMemoryBlock& block = engine.memory_blocks.host_coherent;

      debug_gui.vertex_buffer_offsets[i] = block.stack_pointer;
      block.stack_pointer += align<VkDeviceSize>(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES, block.alignment);

      debug_gui.index_buffer_offsets[i] = block.stack_pointer;
      block.stack_pointer += align<VkDeviceSize>(DebugGui::INDEX_BUFFER_CAPACITY_BYTES, block.alignment);
    }
  }

  rock         = loadGLB(engine, "../assets/rock.glb");
  helmet       = loadGLB(engine, "../assets/DamagedHelmet.glb");
  robot        = loadGLB(engine, "../assets/su-47.glb");
  monster      = loadGLB(engine, "../assets/Monster.glb");
  box          = loadGLB(engine, "../assets/Box.glb");
  animatedBox  = loadGLB(engine, "../assets/BoxAnimated.glb");
  riggedSimple = loadGLB(engine, "../assets/RiggedSimple.glb");
  lil_arrow    = loadGLB(engine, "../assets/lil_arrow.glb");

  helmet_entity.init(ecs, helmet);
  robot_entity.init(ecs, robot);
  monster_entity.init(ecs, monster);

  for (SimpleEntity& entity : box_entities)
    entity.init(ecs, box);

  matrioshka_entity.init(ecs, animatedBox);
  rigged_simple_entity.init(ecs, riggedSimple);

  for (SimpleEntity& entity : axis_arrow_entities)
    entity.init(ecs, lil_arrow);

  {
    int cubemap_size[2] = {512, 512};
    environment_cubemap = generate_cubemap(&engine, this, "../assets/mono_lake.jpg", cubemap_size);
    irradiance_cubemap  = generate_irradiance_cubemap(&engine, this, environment_cubemap, cubemap_size);
    prefiltered_cubemap = generate_prefiltered_cubemap(&engine, this, environment_cubemap, cubemap_size);
    brdf_lookup         = generate_brdf_lookup(&engine, cubemap_size[0]);
  }

  lucida_sans_sdf_image   = engine.load_texture("../assets/lucida_sans_sdf.png");
  sand_albedo             = engine.load_texture("../assets/pbr_sand/sand_albedo.jpg");
  sand_ambient_occlusion  = engine.load_texture("../assets/pbr_sand/sand_ambient_occlusion.jpg");
  sand_metallic_roughness = engine.load_texture("../assets/pbr_sand/sand_metallic_roughness.jpg");
  sand_normal             = engine.load_texture("../assets/pbr_sand/sand_normal.jpg");
  sand_emissive           = engine.load_texture("../assets/pbr_sand/sand_emissive.jpg");

  water_normal = engine.load_texture("../assets/pbr_water/normal_map.jpg");

  const VkDeviceSize light_sources_ubo_size     = sizeof(LightSources);
  const VkDeviceSize skinning_matrices_ubo_size = 64 * sizeof(mat4x4);

  {
    GpuMemoryBlock& block = engine.memory_blocks.host_coherent_ubo;

    for (VkDeviceSize& offset : pbr_dynamic_lights_ubo_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(light_sources_ubo_size, block.alignment);
    }

    for (VkDeviceSize& offset : rig_skinning_matrices_ubo_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(skinning_matrices_ubo_size, block.alignment);
    }

    for (VkDeviceSize& offset : fig_skinning_matrices_ubo_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(skinning_matrices_ubo_size, block.alignment);
    }

    for (VkDeviceSize& offset : monster_skinning_matrices_ubo_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(skinning_matrices_ubo_size, block.alignment);
    }

    for (VkDeviceSize& offset : cascade_view_proj_mat_ubo_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(SHADOWMAP_CASCADE_COUNT * sizeof(mat4x4) + sizeof(vec4), block.alignment);
    }
  }

  {
    GpuMemoryBlock& block = engine.memory_blocks.host_coherent;
    for (VkDeviceSize& offset : green_gui_rulers_buffer_offsets)
    {
      offset = block.stack_pointer;
      block.stack_pointer += align(200 * sizeof(vec2), block.alignment);
    }
  }

  // ----------------------------------------------------------------------------------------------
  // PBR Metallic workflow material descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.pbr_metallic_workflow_material,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &helmet_pbr_material_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &robot_pbr_material_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &sandy_level_pbr_material_dset);
  }

  {
    auto fill_infos = [](const Material& material, VkDescriptorImageInfo infos[5]) {
      infos[0].imageView = material.albedo_texture.image_view;
      infos[1].imageView = material.metal_roughness_texture.image_view;
      infos[2].imageView = material.emissive_texture.image_view;
      infos[3].imageView = material.AO_texture.image_view;
      infos[4].imageView = material.normal_texture.image_view;
    };

    VkDescriptorImageInfo images[5] = {};
    for (VkDescriptorImageInfo& image : images)
    {
      image.sampler     = engine.texture_sampler;
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

    fill_infos(helmet.materials[0], images);
    update.dstSet = helmet_pbr_material_dset, vkUpdateDescriptorSets(engine.device, 1, &update, 0, nullptr);

    fill_infos(robot.materials[0], images);
    update.dstSet = robot_pbr_material_dset, vkUpdateDescriptorSets(engine.device, 1, &update, 0, nullptr);

    Material sand_material = {
        .albedo_texture          = sand_albedo,
        .metal_roughness_texture = sand_metallic_roughness,
        .emissive_texture        = sand_emissive,
        .AO_texture              = sand_ambient_occlusion,
        .normal_texture          = sand_normal,
    };

    fill_infos(sand_material, images);
    update.dstSet = sandy_level_pbr_material_dset;
    vkUpdateDescriptorSets(engine.device, 1, &update, 0, nullptr);
  }

  // ----------------------------------------------------------------------------------------------
  // PBR IBL cubemaps and BRDF lookup table descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.pbr_ibl_cubemaps_and_brdf_lut,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &pbr_ibl_environment_dset);
  }

  {
    VkDescriptorImageInfo cubemap_images[] = {
        {
            .sampler     = engine.texture_sampler,
            .imageView   = irradiance_cubemap.image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        {
            .sampler     = engine.texture_sampler,
            .imageView   = prefiltered_cubemap.image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
    };

    VkDescriptorImageInfo brdf_lut_image = {
        .sampler     = engine.texture_sampler,
        .imageView   = brdf_lookup.image_view,
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

    vkUpdateDescriptorSets(engine.device, SDL_arraysize(writes), writes, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // PBR dynamic light sources descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.pbr_dynamic_lights,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &pbr_dynamic_lights_dset);
  }

  {
    VkDescriptorBufferInfo ubo = {
        .buffer = engine.gpu_host_coherent_ubo_memory_buffer,
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

    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Single texture in fragment shader descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.single_texture_in_frag,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &skybox_cubemap_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &imgui_font_atlas_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &lucida_sans_sdf_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &pbr_water_material_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &debug_shadow_map_dset);
  }

  {
    VkDescriptorImageInfo image = {
        .sampler     = engine.texture_sampler,
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

    image.imageView = debug_gui.font_texture.image_view;
    write.dstSet    = imgui_font_atlas_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);

    image.imageView = environment_cubemap.image_view;
    write.dstSet    = skybox_cubemap_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);

    image.imageView = lucida_sans_sdf_image.image_view;
    write.dstSet    = lucida_sans_sdf_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);

    image.imageView = water_normal.image_view;
    write.dstSet    = pbr_water_material_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);

    image.sampler   = engine.shadowmap_sampler;
    image.imageView = engine.shadowmap_image_view;
    write.dstSet    = debug_shadow_map_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Skinning matrices in vertex shader descriptor sets
  // --------------------------------------------------------------- //

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.skinning_matrices,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &monster_skinning_matrices_dset);
    vkAllocateDescriptorSets(engine.device, &allocate, &rig_skinning_matrices_dset);
  }

  {
    VkDescriptorBufferInfo ubo = {
        .buffer = engine.gpu_host_coherent_ubo_memory_buffer,
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
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);

    write.dstSet = rig_skinning_matrices_dset;
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Cascade shadow map projection matrices - DEPTH PASS
  // --------------------------------------------------------------- ///

  for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.shadow_pass,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &cascade_view_proj_matrices_depth_pass_dset[i]);

    VkDescriptorBufferInfo ubo = {
        .buffer = engine.gpu_host_coherent_ubo_memory_buffer,
        .offset = cascade_view_proj_mat_ubo_offsets[i],
        .range  = SHADOWMAP_CASCADE_COUNT * sizeof(mat4x4),
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &ubo,
    };

    write.dstSet = cascade_view_proj_matrices_depth_pass_dset[i];
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  // --------------------------------------------------------------- //
  // Cascade shadow map projection matrices - RENDERING PASSES
  // --------------------------------------------------------------- ///
  for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.cascade_shadow_map_matrices_ubo_frag,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &cascade_view_proj_matrices_render_dset[i]);

    VkDescriptorBufferInfo ubo = {
        .buffer = engine.gpu_host_coherent_ubo_memory_buffer,
        .offset = cascade_view_proj_mat_ubo_offsets[i],
        .range  = SHADOWMAP_CASCADE_COUNT * sizeof(mat4x4) + sizeof(vec4),
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &ubo,
    };

    write.dstSet = cascade_view_proj_matrices_render_dset[i];
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  vec3_set(robot_position, -2.0f, 3.0f, 3.0f);
  vec3_set(rigged_position, -2.0f, 3.0f, 3.0f);

  cameras.bind_gameplay();

  {
    float extent_width        = static_cast<float>(engine.extent2D.width);
    float extent_height       = static_cast<float>(engine.extent2D.height);
    float aspect_ratio        = extent_width / extent_height;
    float fov                 = to_rad(90.0f);
    float near_clipping_plane = 0.1f;
    float far_clipping_plane  = 1000.0f;
    mat4x4_perspective(cameras.gameplay.projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
    cameras.gameplay.projection[1][1] *= -1.0f;
  }

  {
    float extent_width        = static_cast<float>(engine.extent2D.width);
    float extent_height       = static_cast<float>(engine.extent2D.height);
    float aspect_ratio        = extent_width / extent_height;
    float fov                 = to_rad(90.0f);
    float near_clipping_plane = 0.1f;
    float far_clipping_plane  = 1000.0f;
    mat4x4_perspective(cameras.editor.projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);
    cameras.editor.projection[1][1] *= -1.0f;
  }

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

    engine.memory_blocks.host_visible_transfer_source.stack_pointer = 0;

    VkDeviceSize vertices_host_offset = 0;

    {
      GpuMemoryBlock& block = engine.memory_blocks.host_visible_transfer_source;

      vertices_host_offset = block.stack_pointer;
      block.stack_pointer += align(sizeof(vertices), block.alignment);
    }

    {
      GpuMemoryBlock& block = engine.memory_blocks.device_local;

      green_gui_billboard_vertex_buffer_offset = block.stack_pointer;
      block.stack_pointer += align(sizeof(vertices), block.alignment);
    }

    update_ubo(engine.device, engine.memory_blocks.host_visible_transfer_source.memory, sizeof(vertices),
               vertices_host_offset, vertices);

    VkDeviceSize cg_vertices_host_offset = 0;

    {
      GpuMemoryBlock& block = engine.memory_blocks.host_visible_transfer_source;

      cg_vertices_host_offset = block.stack_pointer;
      block.stack_pointer += align(sizeof(cg_vertices), block.alignment);
    }

    {
      GpuMemoryBlock& block = engine.memory_blocks.device_local;

      regular_billboard_vertex_buffer_offset = block.stack_pointer;
      block.stack_pointer += align(sizeof(cg_vertices), block.alignment);
    }

    update_ubo(engine.device, engine.memory_blocks.host_visible_transfer_source.memory, sizeof(cg_vertices),
               cg_vertices_host_offset, cg_vertices);

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

      vkCmdCopyBuffer(cmd, engine.gpu_host_visible_transfer_source_memory_buffer, engine.gpu_device_local_memory_buffer,
                      SDL_arraysize(copies), copies);
    }

    {
      VkBufferMemoryBarrier barrier = {
          .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .buffer              = engine.gpu_device_local_memory_buffer,
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

  {
    SDL_RWops* ctx              = SDL_RWFromFile("../assets/lucida_sans_sdf.fnt", "r");
    int        fnt_file_size    = static_cast<int>(SDL_RWsize(ctx));
    char*      fnt_file_content = engine.dirty_stack.alloc<char>(fnt_file_size);
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

    engine.dirty_stack.reset();
  }

  DEBUG_VEC2[0] = 96.0f;
  DEBUG_VEC2[1] = -1.0f;

  DEBUG_VEC2_ADDITIONAL[0] = 0.0f;
  DEBUG_VEC2_ADDITIONAL[1] = 0.0f;

  DEBUG_LIGHT_ORTHO_PARAMS[0] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[1] = 10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[2] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[3] = 10.0f;

  vec3_set(light_source_position, -30.0f, -10.0f, 10.0f);

  radar_scale = 0.75f;

  diagnostic_meas_scale = 1.0f;

  vec3_set(cameras.editor.position, 0.0f, -10.0f, -1.0f);

  js.all_threads_idle_signal  = SDL_CreateSemaphore(0);
  js.new_jobs_available_cond  = SDL_CreateCond();
  js.new_jobs_available_mutex = SDL_CreateMutex();
  js.thread_end_requested     = false;

  for (WeaponSelection& sel : weapon_selections)
    sel.init();

  {
    VkCommandBufferAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = engine.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = SWAPCHAIN_IMAGES_COUNT,
    };

    vkAllocateCommandBuffers(engine.device, &info, primary_command_buffers);
  }

  for (VkCommandPool& pool : js.worker_pools)
  {
    VkCommandPoolCreateInfo info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = engine.graphics_family_index,
    };
    vkCreateCommandPool(engine.device, &info, nullptr, &pool);
  }

  for (int swapchain_image = 0; swapchain_image < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image)
  {
    for (int worker_thread = 0; worker_thread < WORKER_THREADS_COUNT; ++worker_thread)
    {
      VkCommandBufferAllocateInfo info = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = js.worker_pools[worker_thread],
          .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
          .commandBufferCount = 64,
      };

      vkAllocateCommandBuffers(engine.device, &info, js.commands[swapchain_image][worker_thread]);
    }
  }

  WorkerThreadData data = {engine, *this};
  for (auto& worker_thread : js.worker_threads)
    worker_thread = SDL_CreateThread(worker_function_decorator, "worker", &data);
  SDL_SemWait(js.all_threads_idle_signal);
  SDL_AtomicSet(&js.threads_finished_work, 0);
}

void Game::teardown(Engine& engine)
{
  vkDeviceWaitIdle(engine.device);

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
    vkDestroyCommandPool(engine.device, pool, nullptr);
}

namespace {

// CASCADE SHADOW MAPPING --------------------------------------------------------------------------------------------
// Based on:
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp
// -------------------------------------------------------------------------------------------------------------------
void recalculate_cascade_view_proj_matrices(mat4x4 cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT],
                                            float  cascade_split_depths[SHADOWMAP_CASCADE_COUNT],
                                            mat4x4 camera_projection, mat4x4 camera_view, vec3 light_source_position)
{
  constexpr float cascade_split_lambda = 0.95f;
  constexpr float near_clip            = 0.1f;
  constexpr float far_clip             = 1000.0f;
  constexpr float clip_range           = far_clip - near_clip;
  constexpr float min_z                = near_clip;
  constexpr float max_z                = near_clip + clip_range;
  constexpr float range                = max_z - min_z;
  constexpr float ratio                = max_z / min_z;

  //
  // This calculates the distances between frustums. For example:
  // near:      0.1
  // far:    1000.0
  // splits: 0.013, 0.034, 0.132, 1.000
  //
  float cascade_splits[SHADOWMAP_CASCADE_COUNT] = {};
  for (uint32_t i = 0; i < SHADOWMAP_CASCADE_COUNT; i++)
  {
    const float p       = static_cast<float>(i + 1) / static_cast<float>(SHADOWMAP_CASCADE_COUNT);
    const float log     = min_z * SDL_powf(ratio, p);
    const float uniform = min_z + range * p;
    const float d       = cascade_split_lambda * (log - uniform) + uniform;
    cascade_splits[i]   = (d - near_clip) / clip_range;
  }

  float last_split_dist = 0.0;
  for (uint32_t cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; cascade_idx++)
  {
    //
    // Frustum edges overview
    //
    //         4 --- 5     Y
    //       /     / |     /\  Z
    //     0 --- 1   |     | /
    //     |     |   6     .--> X
    //     |     | /
    //     3 --- 2
    //
    vec3 frustum_corners[] = {
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, 1.0f},
    };

    //
    // LoD change should follow main game camera and not the light projection.
    // Because of that frustums have to "come out" from viewer camera.
    //
    mat4x4 cam = {};
    mat4x4_mul(cam, camera_projection, camera_view);

    mat4x4 inv_cam = {};
    mat4x4_invert(inv_cam, cam);

    for (vec3& in : frustum_corners)
    {
      vec4 corners_as_vec4 = {in[0], in[1], in[2], 1.0f};
      vec4 inv_corner      = {};
      mat4x4_mul_vec4(inv_corner, inv_cam, corners_as_vec4);
      for (int i = 0; i < 3; ++i)
        in[i] = inv_corner[i] / inv_corner[3];
    }

    const float split_dist = cascade_splits[cascade_idx];
    for (uint32_t i = 0; i < 4; i++)
    {
      vec3 dist = {};
      vec3_sub(dist, frustum_corners[i + 4], frustum_corners[i]);

      vec3 dist_scaled_split_dist = {};
      vec3_scale(dist_scaled_split_dist, dist, split_dist);
      vec3_add(frustum_corners[i + 4], frustum_corners[i], dist_scaled_split_dist);

      vec3 dist_scaled_last_split_dist = {};
      vec3_scale(dist_scaled_last_split_dist, dist, last_split_dist);

      vec3_add(frustum_corners[i], frustum_corners[i], dist_scaled_last_split_dist);
    }

    vec3 frustum_center = {};
    for (vec3& frustum_corner : frustum_corners)
      vec3_add(frustum_center, frustum_center, frustum_corner);

    for (float& i : frustum_center)
      i /= 8.0f;

    float radius = 0.0f;
    for (vec3& frustum_corner : frustum_corners)
    {
      vec3 tmp = {};
      vec3_sub(tmp, frustum_corner, frustum_center);
      float distance = vec3_len(tmp);
      radius         = SDL_max(radius, distance);
    }

    radius           = SDL_ceilf(radius * 16.0f) / 16.0f;
    vec3 max_extents = {radius, radius, radius};
    vec3 min_extents = {-max_extents[0], -max_extents[1], -max_extents[2]};

    vec3 light_dir = {};
    {
      vec3 reflected_light_position = {-light_source_position[0], -light_source_position[1], -light_source_position[2]};
      vec3_norm(light_dir, reflected_light_position);
    }

    mat4x4 light_view_mat = {};
    {
      vec3 up = {0.0f, -1.0f, 0.0f};

      vec3 scaled_light_dir = {};
      vec3_scale(scaled_light_dir, light_dir, -min_extents[2]);

      vec3 eye = {};
      vec3_sub(eye, frustum_center, scaled_light_dir);
      mat4x4_look_at(light_view_mat, eye, frustum_center, up);
    }

    // I don't know why the near clipping plane has to be a huge negative number! If used with 0 as in tutorials,
    // the depth is not calculated properly.. I guess for now it'll have to be this way.
    // @todo: investigate someday (low priority)
    mat4x4 light_ortho_mat = {};
    mat4x4_ortho(light_ortho_mat, min_extents[0], max_extents[0], min_extents[1], max_extents[1], -400.0f,
                 max_extents[2] - min_extents[2]);
    light_ortho_mat[1][1] *= -1.0f;

    mat4x4_mul(cascade_view_proj_mat[cascade_idx], light_ortho_mat, light_view_mat);
    float cascade_split_depth         = near_clip + split_dist * clip_range;
    cascade_split_depths[cascade_idx] = cascade_split_depth;
    last_split_dist                   = cascade_splits[cascade_idx];
  }
}

} // namespace

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
        if (SDL_GetRelativeMouseMode() and cameras.is_gameplay_bound())
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

        auto handle_button_press = [](uint64_t& flags, SDL_Event& event, uint64_t mask) {
          if (SDL_KEYDOWN == event.type)
            flags |= mask;
          else
            flags &= ~mask;
        };

        if (cameras.is_gameplay_bound())
        {
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
            handle_button_press(player_key_flags, event, GameplayKeyFlags::forward_pressed);
            break;
          case SDL_SCANCODE_S:
            handle_button_press(player_key_flags, event, GameplayKeyFlags::back_pressed);
            break;
          case SDL_SCANCODE_A:
            handle_button_press(player_key_flags, event, GameplayKeyFlags::strafe_left_pressed);
            break;
          case SDL_SCANCODE_D:
            handle_button_press(player_key_flags, event, GameplayKeyFlags::strafe_right_pressed);
            break;
          case SDL_SCANCODE_LSHIFT:
            handle_button_press(player_key_flags, event, GameplayKeyFlags::booster_activated);
            break;
          case SDL_SCANCODE_SPACE:
            handle_button_press(player_key_flags, event, GameplayKeyFlags::jump_pressed);
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
            break;
          case SDL_SCANCODE_F3:
            if (SDL_KEYDOWN == event.type)
            {
              cameras.bind_editor();
              SDL_SetRelativeMouseMode(SDL_FALSE);
              vec3_set(cameras.editor.position, player_position[0], player_position[1] - 20.0f, player_position[2]);
            }
            break;
          default:
            break;
          }
        }
        else if (cameras.is_editor_bound())
        {
          switch (event.key.keysym.scancode)
          {
          case SDL_SCANCODE_W:
            handle_button_press(editor_key_flags, event, EditorKeyFlags::up_pressed);
            break;
          case SDL_SCANCODE_S:
            handle_button_press(editor_key_flags, event, EditorKeyFlags::down_pressed);
            break;
          case SDL_SCANCODE_A:
            handle_button_press(editor_key_flags, event, EditorKeyFlags::left_pressed);
            break;
          case SDL_SCANCODE_D:
            handle_button_press(editor_key_flags, event, EditorKeyFlags::right_pressed);
            break;
          case SDL_SCANCODE_ESCAPE:
            quit_requested = true;
            break;
          case SDL_SCANCODE_F3:
            if (SDL_KEYDOWN == event.type)
              cameras.bind_gameplay();
            break;
          default:
            break;
          }
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
    SDL_Window* window = engine.window;
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
    ImGui::PlotHistogram("render times", render_times, SDL_arraysize(render_times), 0, nullptr, 0.0, 0.01,
                         ImVec2(300, 20));

    ImGui::Text("Average update time: %.2fms", 1000.0f * avg(update_times, SDL_arraysize(update_times)));
    ImGui::Text("Average render time: %.2fms", 1000.0f * avg(render_times, SDL_arraysize(render_times)));
  }

  if (ImGui::CollapsingHeader("Animations"))
  {
    ImGui::Text("joint_matrices_stack        ");
    ImGui::SameLine();
    ImGui::ProgressBar(ecs.joint_matrices_stack.usage_percent(512));

    ImGui::Text("node_transforms_stack       ");
    ImGui::SameLine();
    ImGui::ProgressBar(ecs.node_transforms_stack.usage_percent(512));

    ImGui::Text("node_hierarchy_stack        ");
    ImGui::SameLine();
    ImGui::ProgressBar(ecs.node_hierarchy_stack.usage_percent(512));

    ImGui::Text("node_anim_rotations_stack   ");
    ImGui::SameLine();
    ImGui::ProgressBar(ecs.node_anim_rotations_stack.usage_percent(512));

    ImGui::Text("node_anim_translations_stack");
    ImGui::SameLine();
    ImGui::ProgressBar(ecs.node_anim_translations_stack.usage_percent(512));

#if 0
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
#endif

    if (ImGui::Button("monster animation"))
    {
      if (0 == (monster_entity.base.flags & SimpleEntity::AnimationStartTime))
      {
        monster_entity.base.animation_start_time = current_time_sec;
        monster_entity.base.flags |= SimpleEntity::AnimationStartTime;
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

  float          acceleration      = 0.0002f;
  const uint64_t any_movement_mask = GameplayKeyFlags::forward_pressed | GameplayKeyFlags::back_pressed |
                                     GameplayKeyFlags::strafe_left_pressed | GameplayKeyFlags::strafe_right_pressed;

  if ((player_key_flags & GameplayKeyFlags::booster_activated) and (player_key_flags & any_movement_mask))
  {
    if (booster_jet_fuel > 0.0f)
    {
      // booster_jet_fuel -= 0.001f;
      acceleration = 0.0006f;
    }
  }

  if (player_key_flags & GameplayKeyFlags::forward_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle - (float)M_PI / 2) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle - (float)M_PI / 2) * acceleration;
  }
  else if (player_key_flags & GameplayKeyFlags::back_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle + (float)M_PI / 2) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle + (float)M_PI / 2) * acceleration;
  }

  if (player_key_flags & GameplayKeyFlags::strafe_left_pressed)
  {
    player_acceleration[0] += SDL_sinf(camera_angle + (float)M_PI) * acceleration;
    player_acceleration[2] += SDL_cosf(camera_angle + (float)M_PI) * acceleration;
  }
  else if (player_key_flags & GameplayKeyFlags::strafe_right_pressed)
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
          100.0f * get_vr_level_height(-0.1f * player_position[0] + 0.25f, -0.1f * player_position[2] + 0.25f) + 2.25f;
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
        100.0f * get_vr_level_height(-0.1f * player_position[0] + 0.25f, -0.1f * player_position[2] + 0.25f) + 2.25f;

    if (player_key_flags & GameplayKeyFlags::jump_pressed)
    {
      player_jumping                  = true;
      player_jump_start_timestamp_sec = current_time_sec;
    }
  }

  const float camera_distance = 3.0f;
  float       x_camera_offset = SDL_cosf(camera_angle) * camera_distance;
  float       y_camera_offset = SDL_sinf(clamp(camera_updown_angle, -1.5f, 1.5f)) * camera_distance;
  float       z_camera_offset = SDL_sinf(camera_angle) * camera_distance;

  cameras.gameplay.position[0] = player_position[0] + x_camera_offset;
  cameras.gameplay.position[1] = player_position[1] + y_camera_offset - 1.5f;
  cameras.gameplay.position[2] = player_position[2] - z_camera_offset;

  //
  // editor camera movement
  //

  if (editor_key_flags & EditorKeyFlags::up_pressed)
  {
    cameras.editor.position[2] += (0.03f * time_delta_since_last_frame_ms);
  }
  else if (editor_key_flags & EditorKeyFlags::down_pressed)
  {
    cameras.editor.position[2] -= (0.03f * time_delta_since_last_frame_ms);
  }

  if (editor_key_flags & EditorKeyFlags::right_pressed)
  {
    cameras.editor.position[0] += (0.03f * time_delta_since_last_frame_ms);
  }
  else if (editor_key_flags & EditorKeyFlags::left_pressed)
  {
    cameras.editor.position[0] -= (0.03f * time_delta_since_last_frame_ms);
  }

  if (cameras.is_gameplay_bound())
  {
    vec3 center = {player_position[0], player_position[1] - 1.5f, player_position[2]};
    vec3 up     = {0.0f, -1.0f, 0.0f};
    mat4x4_look_at(cameras.gameplay.view, cameras.gameplay.position, center, up);
  }
  else if (cameras.is_editor_bound())
  {
    vec3 center = {cameras.editor.position[0], cameras.editor.position[1] + 5.0f, cameras.editor.position[2] + 0.1f};
    vec3 up     = {0.0f, -1.0f, 0.0f};
    mat4x4_look_at(cameras.editor.view, cameras.editor.position, center, up);
  }

  light_source_position[0] = 200.0f;
  light_source_position[1] = -100.0f;
  light_source_position[2] = 0; // player_position[2];

  if (ImGui::CollapsingHeader("Debug and info"))
  {
    ImGui::Text("Camera mode: ");
    ImGui::SameLine();

    if (ImGui::Button("toggle"))
      cameras.toggle();

    const char* camera_state_text = nullptr;
    if (cameras.is_gameplay_bound())
    {
      camera_state_text = "gameplay";
    }
    else if (cameras.is_editor_bound())
    {
      camera_state_text = "level editor";
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), camera_state_text);

    ImGui::Text("camera offsets: %.2f %.2f %.2f", x_camera_offset, y_camera_offset, z_camera_offset);
    ImGui::Text("camera angles: %.2f %.2f", camera_angle, camera_updown_angle);

    ImGui::Text("position:     %.2f %.2f %.2f", player_position[0], player_position[1], player_position[2]);
    ImGui::Text("camera:       %.2f %.2f %.2f", cameras.gameplay.position[0], cameras.gameplay.position[1],
                cameras.gameplay.position[2]);
    ImGui::Text("acceleration: %.2f %.2f %.2f", player_acceleration[0], player_acceleration[1], player_acceleration[2]);
    ImGui::Text("velocity:     %.2f %.2f %.2f", player_velocity[0], player_velocity[1], player_velocity[2]);
    ImGui::Text("Light:        %.2f %.2f %.2f", light_source_position[0], light_source_position[1],
                light_source_position[2]);
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

  if (ImGui::CollapsingHeader("Memory"))
  {
    auto bytes_as_mb = [](uint32_t in) { return in / (1024u * 1024u); };
    auto calc_frac   = [](VkDeviceSize part, VkDeviceSize max) { return ((float)part / (float)max); };

    ImGui::Text("image memory (%uMB pool)", bytes_as_mb(GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE));
    ImGui::ProgressBar(
        calc_frac(engine.memory_blocks.device_images.stack_pointer, GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE));

    ImGui::Text("device-visible memory (%uMB pool)", bytes_as_mb(GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE));
    ImGui::ProgressBar(
        calc_frac(engine.memory_blocks.device_local.stack_pointer, GPU_DEVICE_LOCAL_IMAGE_MEMORY_POOL_SIZE));

    ImGui::Text("host-visible memory (%uMB pool)", bytes_as_mb(GPU_HOST_COHERENT_MEMORY_POOL_SIZE));
    ImGui::ProgressBar(calc_frac(engine.memory_blocks.host_coherent.stack_pointer, GPU_HOST_COHERENT_MEMORY_POOL_SIZE));

    ImGui::Text("UBO memory (%uMB pool)", bytes_as_mb(GPU_HOST_COHERENT_UBO_MEMORY_POOL_SIZE));
    ImGui::ProgressBar(
        calc_frac(engine.memory_blocks.host_coherent_ubo.stack_pointer, GPU_HOST_COHERENT_UBO_MEMORY_POOL_SIZE));

    ImGui::Text("permanent stack memory (%uMB pool)", bytes_as_mb(HOST_PERMANENT_ALLOCATOR_POOL_SIZE));
    ImGui::ProgressBar(
        calc_frac(static_cast<VkDeviceSize>(engine.permanent_stack.sp), HOST_PERMANENT_ALLOCATOR_POOL_SIZE));
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
  ImGui::InputFloat3("light source position", light_source_position);
  ImGui::InputFloat4("light ortho projection", DEBUG_LIGHT_ORTHO_PARAMS);

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
    vec3 position = {12.8f * SDL_cosf(current_time_sec), 1.0f, -10.0f + (8.8f * SDL_sinf(current_time_sec))};
    vec3 color    = {0.0, 20.0, 0.0};
    update_light(pbr_light_sources_cache, 1, position, color);
  }

  {
    vec3 position = {20.8f * SDL_sinf(current_time_sec / 2.0f), 3.3f,
                     3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f))};
    vec3 color    = {0.0, 0.0, 20.0};
    update_light(pbr_light_sources_cache, 2, position, color);
  }

  {
    vec3 position = {SDL_sinf(current_time_sec / 1.2f), 3.1f, 2.5f * SDL_cosf(current_time_sec / 1.2f)};
    vec3 color    = {8.0, 8.0, 8.0};
    update_light(pbr_light_sources_cache, 3, position, color);
  }

  {
    vec3 position = {0.0f, 3.0f, -4.0f};
    vec3 color    = {10.0, 0.0, 10.0};
    update_light(pbr_light_sources_cache, 4, position, color);
  }

  ImGui::End();

  if (cameras.is_editor_bound())
  {
    ImGui::SetNextWindowSize(ImVec2(engine.extent2D.width / 5, engine.extent2D.height));
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("level editor window (snap?)", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (lmb_clicked)
    {
      ImGui::Text("clicked position");
      ImGui::Text("viewport coordinates: %d %d", lmb_current_cursor_position[0], lmb_current_cursor_position[1]);

      //
      // ~ mouse picking proof of concept ~
      // reference: http://antongerdelan.net/opengl/raycasting.html
      //

      // 3D normalised device coordinates
      const float mouse_x = static_cast<float>(lmb_current_cursor_position[0]);
      const float mouse_y = static_cast<float>(lmb_current_cursor_position[1]);
      const float width   = static_cast<float>(engine.extent2D.width);
      const float height  = static_cast<float>(engine.extent2D.height);
      const float x       = (2.0f * mouse_x) / width - 1.0f;
      const float y       = 1.0f - (2.0f * mouse_y) / height;
      const float z       = 1.0f;
      const vec3  ray_nds = {x, y, z};
      ImGui::Text("ray_nds:  %.2f %.2f %.2f", ray_nds[0], ray_nds[1], ray_nds[2]);

      // 4D homogeneous clip coordinates
      vec4 ray_clip = {ray_nds[0], ray_nds[1], -1.0, 1.0};
      ImGui::Text("ray_clip: %.2f %.2f %.2f %.2f", ray_clip[0], ray_clip[1], ray_clip[2], ray_clip[3]);

      // 4D eye (camera) coordinates
      vec4 ray_eye = {};
      {
        mat4x4 inverted_projection_matrix = {};
        mat4x4_invert(inverted_projection_matrix, cameras.editor.projection);
        mat4x4_mul_vec4(ray_eye, inverted_projection_matrix, ray_clip);
        ray_eye[2] = -1.0f;
        ray_eye[3] = 0.0f;
      }
      ImGui::Text("ray_eye:  %.2f %.2f %.2f %.2f", ray_eye[0], ray_eye[1], ray_eye[2], ray_eye[3]);

      // 4D world coordinates
      vec3 ray_wor = {};
      {
        mat4x4 inverted_view_matrix = {};
        mat4x4_invert(inverted_view_matrix, cameras.editor.view);

        vec4 tmp = {};
        mat4x4_mul_vec4(tmp, inverted_view_matrix, ray_eye);
        vec3_norm(ray_wor, tmp);
      }
      ImGui::Text("ray_wor:  %.2f %.2f %.2f", ray_wor[0], ray_wor[1], ray_wor[2]);
    }

    ImGui::End();
  }
  else if (cameras.is_gameplay_bound())
  {
    ImGui::Begin("thread profiler");
    if (ImGui::Button("pause"))
    {
      if (not js.is_profiling_paused)
      {
        js.paused_profile_data_count = SDL_AtomicGet(&js.profile_each_frame_counter);
        js.paused_profile_data.copy_from(js.profile_each_frame, js.paused_profile_data_count);
      }
      js.is_profiling_paused = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("resume"))
      js.is_profiling_paused = false;

    ImGui::SameLine();
    ImGui::SliderFloat("measurements scale", &diagnostic_meas_scale, 1.0f, 100.0f);

    if (js.is_profiling_paused)
    {
      js.selected_profile_data       = &js.paused_profile_data;
      js.selected_profile_data_count = js.paused_profile_data_count;
    }
    else
    {
      js.selected_profile_data       = &js.profile_each_frame;
      js.selected_profile_data_count = SDL_AtomicGet(&js.profile_each_frame_counter);
    }

    for (int threadId = 0; threadId < static_cast<int>(SDL_arraysize(js.worker_threads)); ++threadId)
    {
      ImGui::Text("worker %d", threadId);
      float     on_thread_duration     = 0.0f;
      bool      first_element_indented = false;
      const int profile_data_count     = js.selected_profile_data_count;
      for (int i = 0; i < profile_data_count; ++i)
      {
        const int profile_thread_id = js.selected_profile_data->thread_ids[i];
        if (threadId == profile_thread_id)
        {
          const float profile_duration_sec = js.selected_profile_data->duration_sec[i];
          const float scaled_duration      = diagnostic_meas_scale * profile_duration_sec;
          on_thread_duration += profile_duration_sec;

          if (not first_element_indented)
          {
            ImGui::SameLine();
            first_element_indented = true;
          }
          else
          {
            ImGui::SameLine(0.0f, 0.1f);
          }

          const char* profile_job_name = js.selected_profile_data->job_names[i];
          ImGui::ColorButton(profile_job_name, ImVec4((float)i / (float)profile_data_count, 0.1f, 0.1, 1.0f), 0,
                             ImVec2(100.0f * 1000.0f * scaled_duration, 0));

          if (ImGui::IsItemHovered())
          {
            ImGui::SetTooltip("name: %s\n%.8f sec\n%.2f ms", profile_job_name, profile_duration_sec,
                              1000.0f * profile_duration_sec);
          }
        }
      }
      ImGui::Text("total: %.5fms", 1000.0f * on_thread_duration);
      ImGui::Separator();
    }
    ImGui::End();
  }

  recalculate_cascade_view_proj_matrices(cascade_view_proj_mat, cascade_split_depths, cameras.current->projection,
                                         cameras.current->view, light_source_position);

  js.jobs_max = 0;
  js.push("moving lights update", update::moving_lights_job);
  js.push("helmet update", update::helmet_job);
  js.push("robot update", update::robot_job);
  js.push("monster update", update::monster_job);
  js.push("rigged simple update", update::rigged_simple_job);
  js.push("matrioshka update", update::matrioshka_job);

  if (cameras.is_editor_bound())
    js.push("orientation axis update", update::orientation_axis_job);

  SDL_AtomicSet(&js.profile_each_frame_counter, 0);
  SDL_LockMutex(js.new_jobs_available_mutex);
  SDL_CondBroadcast(js.new_jobs_available_cond);
  SDL_UnlockMutex(js.new_jobs_available_mutex);

  ImGui::Render();

  SDL_SemWait(js.all_threads_idle_signal);
  SDL_AtomicSet(&js.threads_finished_work, 0);
  SDL_AtomicSet(&js.jobs_taken, 0);
}

void Game::render(Engine& engine)
{
  vkAcquireNextImageKHR(engine.device, engine.swapchain, UINT64_MAX, engine.image_available, VK_NULL_HANDLE,
                        &image_index);
  vkWaitForFences(engine.device, 1, &engine.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.device, 1, &engine.submition_fences[image_index]);

  for (int worker_thread = 0; worker_thread < WORKER_THREADS_COUNT; ++worker_thread)
  {
    int count = js.submited_command_count[image_index][worker_thread];
    for (int i = 0; i < count; ++i)
      vkResetCommandBuffer(js.commands[image_index][worker_thread][i], 0);
    js.submited_command_count[image_index][worker_thread] = 0;
  }

  {
    FunctionTimer timer(render_times, SDL_arraysize(render_times));

    js.jobs_max = 0;
    js.push("skybox", render::skybox_job);
    js.push("robot", render::robot_job);
    js.push("helmet", render::helmet_job);
    js.push("point lights", render::point_light_boxes);
    js.push("box", render::matrioshka_box);
    js.push("vr scene", render::vr_scene);
    js.push("water", render::water);
    js.push("simple rigged", render::simple_rigged);
    js.push("monster", render::monster_rigged);
    js.push("robot depth", render::robot_depth_job);
    js.push("helmet depth", render::helmet_depth_job);
    // js.push("vr scene depth", render::vr_scene_depth);

    if (cameras.is_gameplay_bound())
    {
      js.push("radar", render::radar);
      js.push("gui lines", render::robot_gui_lines);
      js.push("gui height ruler text", render::height_ruler_text);
      js.push("gui tilt ruler text", render::tilt_ruler_text);
      js.push("speed meter", render::robot_gui_speed_meter_text);
      js.push("speed meter triangle", render::robot_gui_speed_meter_triangle);
      js.push("compass text", render::compass_text);
      js.push("radar dots", render::radar_dots);
      js.push("weapon selectors - left", render::weapon_selectors_left);
      js.push("weapon selectors - right", render::weapon_selectors_right);
    }
    else
    {
      js.push("orientation axis", render::orientation_axis);
    }

    js.push("imgui", render::imgui);
    // js.push("debug shadow map depth pass", render::debug_shadowmap);

    SDL_LockMutex(js.new_jobs_available_mutex);
    SDL_CondBroadcast(js.new_jobs_available_cond);
    SDL_UnlockMutex(js.new_jobs_available_mutex);
    // While we await for tasks to be finished by worker threads, this one will handle memory synchronization

    //
    // Cascade shadow map projection matrices
    //
    {
      struct Update
      {
        mat4x4 cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT];
        vec4   cascade_splits;
      } ubo_update = {};

      for (int i = 0; i < SHADOWMAP_CASCADE_COUNT; ++i)
        mat4x4_dup(ubo_update.cascade_view_proj_mat[i], cascade_view_proj_mat[i]);

      for (int i = 0; i < SHADOWMAP_CASCADE_COUNT; ++i)
        ubo_update.cascade_splits[i] = cascade_split_depths[i];

      update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory, sizeof(ubo_update),
                 cascade_view_proj_mat_ubo_offsets[image_index], &ubo_update);
    }

    //
    // light sources
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory, sizeof(LightSources),
               pbr_dynamic_lights_ubo_offsets[image_index], &pbr_light_sources_cache);

    //
    // rigged simple skinning matrices
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
               riggedSimple.skins[0].joints.count * sizeof(mat4x4), rig_skinning_matrices_ubo_offsets[image_index],
               &ecs.joint_matrices[rigged_simple_entity.joint_matrices]);

    //
    // monster skinning matrices
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
               monster.skins[0].joints.count * sizeof(mat4x4), monster_skinning_matrices_ubo_offsets[image_index],
               &ecs.joint_matrices[monster_entity.joint_matrices]);

    {
      GenerateGuiLinesCommand cmd = {
          .player_y_location_meters = -(2.0f - player_position[1]),
          .camera_x_pitch_radians = 0.0f, // to_rad(10) * SDL_sinf(current_time_sec), // simulating future strafe tilts,
          .camera_y_pitch_radians = camera_updown_angle,
      };

      ArrayView<GuiLine> r = {};

      {
        generate_gui_lines(cmd, nullptr, &r.count);
        r.data = engine.dirty_stack.alloc<GuiLine>(r.count);
        generate_gui_lines(cmd, r.data, &r.count);
      }

      float* pushed_lines_data    = nullptr;
      int    pushed_lines_counter = 0;
      pushed_lines_data           = engine.dirty_stack.alloc<float>(4 * r.count);

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

      update_ubo(engine.device, engine.memory_blocks.host_coherent.memory, r.count * 2 * sizeof(vec2),
                 green_gui_rulers_buffer_offsets[image_index], pushed_lines_data);
      engine.dirty_stack.reset();
    }

    ImDrawData* draw_data = ImGui::GetDrawData();

    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    SDL_assert(Game::DebugGui::VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
    SDL_assert(Game::DebugGui::INDEX_BUFFER_CAPACITY_BYTES >= index_size);

    if (0 < vertex_size)
    {
      ImDrawVert* vtx_dst = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_coherent.memory,
                  debug_gui.vertex_buffer_offsets[image_index], vertex_size, 0, reinterpret_cast<void**>(&vtx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx_dst += cmd_list->VtxBuffer.Size;
      }

      vkUnmapMemory(engine.device, engine.memory_blocks.host_coherent.memory);
    }

    if (0 < index_size)
    {
      ImDrawIdx* idx_dst = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_coherent.memory, debug_gui.index_buffer_offsets[image_index],
                  index_size, 0, reinterpret_cast<void**>(&idx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
      }

      vkUnmapMemory(engine.device, engine.memory_blocks.host_coherent.memory);
    }

    SDL_SemWait(js.all_threads_idle_signal);
    SDL_AtomicSet(&js.threads_finished_work, 0);
    SDL_AtomicSet(&js.jobs_taken, 0);

    {
      VkCommandBuffer cmd = primary_command_buffers[image_index];

      {
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &begin);
      }

      // -----------------------------------------------------------------------------------------------
      // SHADOW MAPPING PASS
      // -----------------------------------------------------------------------------------------------
      for (int cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; ++cascade_idx)
      {
        VkClearValue clear_value = {.depthStencil = {1.0, 0}};

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.shadowmap.render_pass,
            .framebuffer     = engine.render_passes.shadowmap.framebuffers[cascade_idx],
            .renderArea      = {.extent = {.width = SHADOWMAP_IMAGE_DIM, .height = SHADOWMAP_IMAGE_DIM}},
            .clearValueCount = 1,
            .pClearValues    = &clear_value,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        for (const ShadowmapCommandBuffer& iter : shadow_mapping_pass_commands)
          if (cascade_idx == iter.cascade_idx)
            vkCmdExecuteCommands(cmd, 1, &iter.cmd);

        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // SKYBOX PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 1u : 2u;
        VkClearValue   clear_values[2]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color = {{0.0f, 0.0f, 0.2f, 1.0f}};
        }
        else
        {
          clear_values[0].color = {{0.0f, 0.0f, 0.2f, 1.0f}};
          clear_values[1].color = {{0.0f, 0.0f, 0.2f, 1.0f}};
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.skybox.render_pass,
            .framebuffer     = engine.render_passes.skybox.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(cmd, 1, &skybox_command);
        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // SCENE PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 2u : 3u;
        VkClearValue   clear_values[3]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color        = {{0.0f, 0.0f, 0.2f, 1.0f}};
          clear_values[1].depthStencil = {1.0, 0};
        }
        else
        {
          clear_values[0].color        = {{0.0f, 0.0f, 0.2f, 1.0f}};
          clear_values[1].depthStencil = {1.0, 0};
          clear_values[2].color        = {{0.0f, 0.0f, 0.2f, 1.0f}};
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.color_and_depth.render_pass,
            .framebuffer     = engine.render_passes.color_and_depth.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        for (VkCommandBuffer secondary_command : scene_rendering_commands)
          vkCmdExecuteCommands(cmd, 1, &secondary_command);
        vkCmdEndRenderPass(cmd);
      }

      // -----------------------------------------------------------------------------------------------
      // GUI PASS
      // -----------------------------------------------------------------------------------------------
      {
        const uint32_t clear_values_count = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 1u : 2u;
        VkClearValue   clear_values[2]    = {};

        if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
        {
          clear_values[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
        }
        else
        {
          clear_values[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
          clear_values[1].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
        }

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = engine.render_passes.gui.render_pass,
            .framebuffer     = engine.render_passes.gui.framebuffers[image_index],
            .renderArea      = {.extent = engine.extent2D},
            .clearValueCount = clear_values_count,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        for (VkCommandBuffer secondary_command : gui_commands)
          vkCmdExecuteCommands(cmd, 1, &secondary_command);
        vkCmdEndRenderPass(cmd);
      }

      {
        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = 0,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = engine.shadowmap_image,
            .subresourceRange =
                {
                    .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = SHADOWMAP_CASCADE_COUNT,
                },
        };

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);
      }

      vkEndCommandBuffer(cmd);
    }

    shadow_mapping_pass_commands.reset();
    scene_rendering_commands.reset();
    gui_commands.reset();
  }

  {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &engine.image_available,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &primary_command_buffers[image_index],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &engine.render_finished,
    };

    vkQueueSubmit(engine.graphics_queue, 1, &submit, engine.submition_fences[image_index]);
  }

  VkPresentInfoKHR present = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &engine.render_finished,
      .swapchainCount     = 1,
      .pSwapchains        = &engine.swapchain,
      .pImageIndices      = &image_index,
  };

  vkQueuePresentKHR(engine.graphics_queue, &present);
}
