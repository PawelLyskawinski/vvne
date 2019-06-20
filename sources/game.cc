#include "game.hh"
#include "engine/cubemap.hh"
#include "engine/free_list_visualizer.hh"
#include "engine/gpu_memory_visualizer.hh"
#include "render_jobs.hh"
#include "terrain_as_a_function.hh"
#include "update_jobs.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_vulkan.h>

static void update_ubo(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, VkDeviceSize offset, void* src)
{
  void* data = nullptr;
  vkMapMemory(device, memory, offset, size, 0, &data);
  SDL_memcpy(data, src, size);
  vkUnmapMemory(device, memory);
}

namespace {

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
  }

  materials.setup(engine);
  materials.light_source_position = Vec3(0.0f, -1.0f, 1.0f);

  helmet_entity.init(engine.generic_allocator, materials.helmet);
  robot_entity.init(engine.generic_allocator, materials.robot);
  monster_entity.init(engine.generic_allocator, materials.monster);

  for (SimpleEntity& entity : box_entities)
    entity.init(engine.generic_allocator, materials.box);

  matrioshka_entity.init(engine.generic_allocator, materials.animatedBox);
  rigged_simple_entity.init(engine.generic_allocator, materials.riggedSimple);

  for (SimpleEntity& entity : axis_arrow_entities)
    entity.init(engine.generic_allocator, materials.lil_arrow);

  player.setup(engine.extent2D.width, engine.extent2D.height);
  booster_jet_fuel = 1.0f;

  DEBUG_VEC2[0] = 0.1f;
  DEBUG_VEC2[1] = -1.0f;

  DEBUG_VEC2_ADDITIONAL[0] = 0.0f;
  DEBUG_VEC2_ADDITIONAL[1] = 0.0f;

  DEBUG_LIGHT_ORTHO_PARAMS[0] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[1] = 10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[2] = -10.0f;
  DEBUG_LIGHT_ORTHO_PARAMS[3] = 10.0f;

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

  job_context.engine          = &engine;
  job_context.game            = this;
  engine.job_system.user_data = &job_context;
}

void Game::teardown(Engine& engine)
{
  for (SDL_Cursor* cursor : debug_gui.mousecursors)
    SDL_FreeCursor(cursor);
  materials.teardown(engine);

  vkDeviceWaitIdle(engine.device);
}

// CASCADE SHADOW MAPPING --------------------------------------------------------------------------------------------
// Based on:
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp
// -------------------------------------------------------------------------------------------------------------------
static void recalculate_cascade_view_proj_matrices(Mat4x4* cascade_view_proj_mat, float* cascade_split_depths,
                                            Mat4x4 camera_projection, Mat4x4 camera_view, Vec3 light_source_position)
{
  constexpr float cascade_split_lambda = 0.95f;
  constexpr float near_clip            = 0.001f;
  constexpr float far_clip             = 500.0f;
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
    Vec3 frustum_corners[] = {
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, 1.0f},
    };

    //
    // LoD change should follow main game camera and not the light projection.
    // Because of that frustums have to "come out" from viewer camera.
    //
    Mat4x4 inv_cam = (camera_projection * camera_view).invert();

    for (Vec3& in : frustum_corners)
    {
      Vec4 inv_corner = inv_cam * Vec4(in, 1.0f);
      in              = inv_corner.as_vec3().scale(1.0f / inv_corner.w);
    }

    const float split_dist = cascade_splits[cascade_idx];
    for (uint32_t i = 0; i < 4; i++)
    {
      const Vec3 dist        = frustum_corners[i + 4] - frustum_corners[i];
      frustum_corners[i + 4] = frustum_corners[i] + dist.scale(split_dist);
      frustum_corners[i] += dist.scale(last_split_dist);
    }

    Vec3 frustum_center;
    for (Vec3& frustum_corner : frustum_corners)
    {
      frustum_center += frustum_corner;
    }
    frustum_center = frustum_center.scale(1.0f / 8.0f);

    float radius = 0.0f;
    for (const Vec3& frustum_corner : frustum_corners)
    {
      const float distance = (frustum_corner - frustum_center).len();
      radius               = SDL_max(radius, distance);
    }

    Vec3 max_extents = Vec3(SDL_ceilf(radius * 16.0f) / 16.0f);
    Vec3 min_extents = max_extents.invert_signs();
    Vec3 light_dir   = light_source_position.invert_signs().normalize();

    Mat4x4 light_view_mat;

    {
      Vec3 up  = Vec3(0.0f, -1.0f, 0.0f);
      Vec3 eye = frustum_center - light_dir.scale(-min_extents.z);
      light_view_mat.look_at(eye, frustum_center, up);
    }

    // todo: I don't know why the near clipping plane has to be a huge negative number! If used with 0 as in tutorials,
    //       the depth is not calculated properly.. I guess for now it'll have to be this way.

    Mat4x4 light_ortho_mat;
    light_ortho_mat.ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, -50.0f,
                          max_extents.z - min_extents.z);

    cascade_view_proj_mat[cascade_idx] = light_ortho_mat * light_view_mat;
    float cascade_split_depth          = near_clip + split_dist * clip_range;
    cascade_split_depths[cascade_idx]  = cascade_split_depth;
    last_split_dist                    = cascade_splits[cascade_idx];
  }
}

void Game::update(Engine& engine, float time_delta_since_last_frame_ms)
{
  update_profiler.on_frame();
  render_profiler.on_frame();
  ScopedPerfEvent perf_event(update_profiler, __PRETTY_FUNCTION__, 0);

  {
    bool      quit_requested = false;
    SDL_Event event          = {};
    while (SDL_PollEvent(&event))
    {
      player.process_event(event);
      debug_gui.process_event(event);
      switch (event.type)
      {
      case SDL_MOUSEBUTTONDOWN:
      {
        if (SDL_BUTTON_LEFT == event.button.button)
        {
          lmb_clicked = true;
          SDL_GetMouseState(&lmb_last_cursor_position[0], &lmb_last_cursor_position[1]);
          lmb_current_cursor_position[0] = lmb_last_cursor_position[0];
          lmb_current_cursor_position[1] = lmb_last_cursor_position[1];
        }
      }
      break;

      case SDL_MOUSEMOTION:
      {
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
        case SDL_SCANCODE_ESCAPE:
          quit_requested = true;
          break;
        default:
          break;
        }
      }

      break;

      default:
        break;
      }
    }

    if (quit_requested)
    {
      SDL_Event event = {.type = SDL_QUIT};
      SDL_PushEvent(&event);
    }
  }

  for (WeaponSelection& sel : weapon_selections)
    sel.animate(0.008f * time_delta_since_last_frame_ms);


  debug_gui.update(engine, *this);
  player.update(current_time_sec, time_delta_since_last_frame_ms);

  materials.pbr_light_sources_cache.count = 5;

  auto calculate_light_Y_pos = [](Vec3& position) {
    const float y_scale    = 2.0f;
    const float y_offset   = -11.0f;
    const float adjustment = 0.1f;

    position.y = SDL_cosf(adjustment * position.x) + SDL_cosf(adjustment * position.y);
    position.y *= -y_scale;
    position.y -= y_offset;
  };

  {
    Vec3 position = Vec3(SDL_sinf(current_time_sec), 3.5f, 3.0f + SDL_cosf(current_time_sec));
    calculate_light_Y_pos(position);
    const Vec3 color = Vec3(20.0f + (5.0f * SDL_sinf(current_time_sec + 0.4f)), 0.0, 0.0);
    materials.pbr_light_sources_cache.update(0, position, color);
  }

  {
    Vec3 position = Vec3(12.8f * SDL_cosf(current_time_sec), 1.0f, -10.0f + (8.8f * SDL_sinf(current_time_sec)));
    calculate_light_Y_pos(position);
    const Vec3 color = Vec3(0.0f, 20.0f, 0.0f);
    materials.pbr_light_sources_cache.update(1, position, color);
  }

  {
    Vec3 position =
        Vec3(20.8f * SDL_sinf(current_time_sec / 2.0f), 3.3f, 3.0f + (0.8f * SDL_cosf(current_time_sec / 2.0f)));
    calculate_light_Y_pos(position);
    const Vec3 color = Vec3(0.0, 0.0, 20.0);
    materials.pbr_light_sources_cache.update(2, position, color);
  }

  {
    Vec3 position = Vec3(SDL_sinf(current_time_sec / 1.2f), 3.1f, 2.5f * SDL_cosf(current_time_sec / 1.2f));
    calculate_light_Y_pos(position);
    const Vec3 color = Vec3(8.0);
    materials.pbr_light_sources_cache.update(3, position, color);
  }

  {
    Vec3 position = Vec3(0.0f, 3.0f, -4.0f);
    calculate_light_Y_pos(position);
    const Vec3 color = Vec3(10.0f, 0.0f, 10.0f);
    materials.pbr_light_sources_cache.update(4, position, color);
  }

  recalculate_cascade_view_proj_matrices(materials.cascade_view_proj_mat, materials.cascade_split_depths,
                                         player.camera_projection, player.camera_view, materials.light_source_position);

  Job jobs[] = {
      update::moving_lights_job, update::helmet_job,        update::robot_job,
      update::monster_job,       update::rigged_simple_job, update::matrioshka_job,
  };

  engine.job_system.jobs.reset();
  engine.job_system.jobs.push(jobs, SDL_arraysize(jobs));
  engine.job_system.start();
  ImGui::Render();
  engine.job_system.wait_for_finish();
}

namespace {

void frustum_planes_generate(const Mat4x4& matrix, Vec4 planes[])
{
  enum
  {
    LEFT   = 0,
    RIGHT  = 1,
    TOP    = 2,
    BOTTOM = 3,
    BACK   = 4,
    FRONT  = 5
  };

  planes[LEFT].x   = matrix.at(0, 3) + matrix.at(0, 0);
  planes[LEFT].y   = matrix.at(1, 3) + matrix.at(1, 0);
  planes[LEFT].z   = matrix.at(2, 3) + matrix.at(2, 0);
  planes[LEFT].w   = matrix.at(3, 3) + matrix.at(3, 0);
  planes[RIGHT].x  = matrix.at(0, 3) - matrix.at(0, 0);
  planes[RIGHT].y  = matrix.at(1, 3) - matrix.at(1, 0);
  planes[RIGHT].z  = matrix.at(2, 3) - matrix.at(2, 0);
  planes[RIGHT].w  = matrix.at(3, 3) - matrix.at(3, 0);
  planes[TOP].x    = matrix.at(0, 3) - matrix.at(0, 1);
  planes[TOP].y    = matrix.at(1, 3) - matrix.at(1, 1);
  planes[TOP].z    = matrix.at(2, 3) - matrix.at(2, 1);
  planes[TOP].w    = matrix.at(3, 3) - matrix.at(3, 1);
  planes[BOTTOM].x = matrix.at(0, 3) + matrix.at(0, 1);
  planes[BOTTOM].y = matrix.at(1, 3) + matrix.at(1, 1);
  planes[BOTTOM].z = matrix.at(2, 3) + matrix.at(2, 1);
  planes[BOTTOM].w = matrix.at(3, 3) + matrix.at(3, 1);
  planes[BACK].x   = matrix.at(0, 3) + matrix.at(0, 2);
  planes[BACK].y   = matrix.at(1, 3) + matrix.at(1, 2);
  planes[BACK].z   = matrix.at(2, 3) + matrix.at(2, 2);
  planes[BACK].w   = matrix.at(3, 3) + matrix.at(3, 2);
  planes[FRONT].x  = matrix.at(0, 3) - matrix.at(0, 2);
  planes[FRONT].y  = matrix.at(1, 3) - matrix.at(1, 2);
  planes[FRONT].z  = matrix.at(2, 3) - matrix.at(2, 2);
  planes[FRONT].w  = matrix.at(3, 3) - matrix.at(3, 2);

  for (auto i = 0; i < 6; i++)
  {
    const float length = planes[i].as_vec3().len();
    planes[i]          = planes[i].scale(1.0f / length);
  }
}

} // namespace

void Game::render(Engine& engine)
{
  vkAcquireNextImageKHR(engine.device, engine.swapchain, UINT64_MAX, engine.image_available, VK_NULL_HANDLE,
                        &image_index);
  vkWaitForFences(engine.device, 1, &engine.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.device, 1, &engine.submition_fences[image_index]);

  engine.job_system.reset_command_buffers(image_index);

  {
    ScopedPerfEvent perf_event(render_profiler, __PRETTY_FUNCTION__, 0);

    Job gameplay_jobs[] = {
        render::radar,
        render::robot_gui_lines,
        render::height_ruler_text,
        render::tilt_ruler_text,
        render::robot_gui_speed_meter_text,
        render::robot_gui_speed_meter_triangle,
        render::compass_text,
        render::radar_dots,
        render::weapon_selectors_left,
        render::weapon_selectors_right,
    };

    Job jobs[] = {
        render::tesselated_ground,
        render::skybox_job,
        render::robot_job,
        render::helmet_job,
        render::point_light_boxes,
        render::matrioshka_box,
        render::water,
        render::simple_rigged,
        render::monster_rigged,
        render::robot_depth_job,
        render::helmet_depth_job,
        render::imgui,
        render::debug_shadowmap,
    };

    engine.job_system.jobs.reset();
    engine.job_system.jobs.push(gameplay_jobs, SDL_arraysize(gameplay_jobs));
    engine.job_system.jobs.push(jobs, SDL_arraysize(jobs));
    engine.job_system.start();

    // While we await for tasks to be finished by worker threads, this one will handle memory synchronization

    //
    // Cascade shadow map projection matrices
    //
    {
      struct Update
      {
        Mat4x4 cascade_view_proj_mat[SHADOWMAP_CASCADE_COUNT];
        float  cascade_splits[SHADOWMAP_CASCADE_COUNT];
      } ubo_update = {};

      for (int i = 0; i < SHADOWMAP_CASCADE_COUNT; ++i)
      {
        ubo_update.cascade_view_proj_mat[i] = materials.cascade_view_proj_mat[i];
      }

      for (int i = 0; i < SHADOWMAP_CASCADE_COUNT; ++i)
      {
        ubo_update.cascade_splits[i] = materials.cascade_split_depths[i];
      }

      update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory, sizeof(ubo_update),
                 materials.cascade_view_proj_mat_ubo_offsets[image_index], &ubo_update);
    }

    //
    // light sources
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory, sizeof(LightSources),
               materials.pbr_dynamic_lights_ubo_offsets[image_index], &materials.pbr_light_sources_cache);

    //
    // rigged simple skinning matrices
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
               materials.riggedSimple.skins[0].joints.count * sizeof(mat4x4),
               materials.rig_skinning_matrices_ubo_offsets[image_index], rigged_simple_entity.joint_matrices);

    //
    // monster skinning matrices
    //
    update_ubo(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
               materials.monster.skins[0].joints.count * sizeof(mat4x4),
               materials.monster_skinning_matrices_ubo_offsets[image_index], monster_entity.joint_matrices);

    //
    // frustum planes
    //
    {
      void* data = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_coherent_ubo.memory,
                  materials.frustum_planes_ubo_offsets[image_index], 6 * sizeof(vec4), 0, &data);
      frustum_planes_generate(player.camera_projection * player.camera_view, reinterpret_cast<Vec4*>(data));
      vkUnmapMemory(engine.device, engine.memory_blocks.host_coherent_ubo.memory);
    }

    engine.job_system.wait_for_finish();
    debug_gui.render(engine, *this);

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
            .image               = engine.shadowmap_image.image,
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
