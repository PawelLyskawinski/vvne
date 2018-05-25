#include "game.hh"
#include "cubemap.hh"
#include "level_generator_vr.hh"
#include "utility.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_timer.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

float normalize_quat(quat q)
{
  float x = q[0] * q[0];
  float y = q[1] * q[1];
  float z = q[2] * q[2];
  float w = q[3] * q[3];

  float norm = SDL_sqrtf(x + y + z + w);
  q[0] /= norm;
  q[1] /= norm;
  q[2] /= norm;
}

float clamp(float val, float min, float max)
{
  return (val < min) ? min : (val > max) ? max : val;
}

void quat_copy(quat into, quat from)
{
  for (int i = 0; i < 4; ++i)
    into[i] = from[i];
}

void slerp(quat a, quat b, quat c, float time)
{
  float       dotproduct = quat_inner_product(a, b);
  const float limit      = 0.9995f;

  if (dotproduct > limit)
  {
    quat_mul(c, b, a);
    normalize_quat(c);
  }
  else
  {
    dotproduct   = clamp(dotproduct, -1.0f, 1.0f);
    float theta0 = static_cast<float>(SDL_acos(dotproduct));
    float theta  = theta0 * time;

    quat tmp = {};
    quat_copy(tmp, a);
    quat_scale(tmp, tmp, dotproduct);

    quat v2 = {};
    quat_mul(v2, b, tmp);
    normalize_quat(v2);

    quat a_scaled = {};
    quat_scale(a_scaled, a, SDL_cosf(theta));

    quat v2_scaled = {};
    quat_scale(v2_scaled, v2, SDL_sinf(theta));

    quat_add(c, a_scaled, v2_scaled);
  }
}

int find_first_higher(float* times, float current)
{
  int iter = 0;
  while (current > times[iter])
    iter += 1;
  return iter;
}

void quat_lerp(float* a, float* b, float* c, float time)
{
  float reminder_time = 1.0f - time;
  for (int i = 0; i < 4; ++i)
  {
    c[i] = reminder_time * a[i] + time * b[i];
  }
  vec4_norm(c, c);
}

void lerp(float* a, float* b, float* c, int len, float time)
{
  for (int i = 0; i < len; ++i)
  {
    float difference = b[i] - a[i];
    float progressed = difference * time;
    c[i]             = a[i] + progressed;
  }
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
  riggedFigure.loadGLB(engine, "../assets/RiggedFigure.glb");
  monster.loadGLB(engine, "../assets/Monster.glb");

  lights_ubo_offset = engine.ubo_host_visible.allocate(sizeof(light_sources));

  {
    CubemapGenerator generator{};
    generator.filepath        = "../assets/old_industrial_hall.jpg";
    generator.engine          = &engine;
    generator.game            = this;
    generator.desired_size[0] = 512;
    generator.desired_size[1] = 512;

    environment_cubemap_idx = generator.generate();
  }

  {
    IrradianceGenerator generator{};
    generator.environment_cubemap_idx = environment_cubemap_idx;
    generator.engine                  = &engine;
    generator.game                    = this;
    generator.desired_size[0]         = 512;
    generator.desired_size[1]         = 512;

    irradiance_cubemap_idx = generator.generate();
  }

  {
    PrefilteredCubemapGenerator generator{};
    generator.environment_cubemap_idx = environment_cubemap_idx;
    generator.engine                  = &engine;
    generator.game                    = this;
    generator.desired_size[0]         = 512;
    generator.desired_size[1]         = 512;

    prefiltered_cubemap_idx = generator.generate();
  }

  brdf_lookup_idx = generateBRDFlookup(&engine, 512);

  // ----------------------------------------------------------------------------------------------
  // Descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool     = engine.generic_handles.descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts        = &engine.simple_rendering.descriptor_set_layout;

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &skybox_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &helmet_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &imgui_dset);

    for (int i = 0; i < SDL_arraysize(rig_dsets); ++i)
    {
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &rig_dsets[i]);
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &fig_dsets[i]);
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &monster_dsets[i]);
    }
  }

  {
    VkDescriptorImageInfo skybox_image{};
    skybox_image.sampler     = engine.generic_handles.texture_sampler;
    skybox_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    skybox_image.imageView   = engine.images.image_views[environment_cubemap_idx];

    VkDescriptorImageInfo imgui_image{};
    imgui_image.sampler     = engine.generic_handles.texture_sampler;
    imgui_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgui_image.imageView   = engine.images.image_views[debug_gui.font_texture_idx];

    VkDescriptorImageInfo helmet_images[8]{};
    for (VkDescriptorImageInfo& info : helmet_images)
    {
      info.sampler     = engine.generic_handles.texture_sampler;
      info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // todo: refactor

    {
      const Material& material   = helmet.scene_graph.materials.data[0];
      helmet_images[0].imageView = engine.images.image_views[material.albedo_texture_idx];
      helmet_images[1].imageView = engine.images.image_views[material.metal_roughness_texture_idx];
      helmet_images[2].imageView = engine.images.image_views[material.emissive_texture_idx];
      helmet_images[3].imageView = engine.images.image_views[material.AO_texture_idx];
      helmet_images[4].imageView = engine.images.image_views[material.normal_texture_idx];
    }

    helmet_images[5].imageView = engine.images.image_views[irradiance_cubemap_idx];
    helmet_images[6].imageView = engine.images.image_views[prefiltered_cubemap_idx];
    helmet_images[7].imageView = engine.images.image_views[brdf_lookup_idx];

    VkDescriptorBufferInfo helmet_ubo{};
    helmet_ubo.buffer = engine.ubo_host_visible.buffer;
    helmet_ubo.offset = lights_ubo_offset;
    helmet_ubo.range  = sizeof(light_sources);

    VkWriteDescriptorSet writes[4]{};

    VkWriteDescriptorSet& skybox_write = writes[0];
    skybox_write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    skybox_write.dstBinding            = 0;
    skybox_write.dstArrayElement       = 0;
    skybox_write.descriptorType        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skybox_write.descriptorCount       = 1;
    skybox_write.pImageInfo            = &skybox_image;
    skybox_write.dstSet                = skybox_dset;

    VkWriteDescriptorSet& helmet_write = writes[1];
    helmet_write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    helmet_write.dstBinding            = 0;
    helmet_write.dstArrayElement       = 0;
    helmet_write.descriptorType        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    helmet_write.descriptorCount       = SDL_arraysize(helmet_images);
    helmet_write.pImageInfo            = helmet_images;
    helmet_write.dstSet                = helmet_dset;

    VkWriteDescriptorSet& helmet_ubo_write = writes[2];
    helmet_ubo_write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    helmet_ubo_write.dstBinding            = 8;
    helmet_ubo_write.dstArrayElement       = 0;
    helmet_ubo_write.descriptorType        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    helmet_ubo_write.descriptorCount       = 1;
    helmet_ubo_write.pBufferInfo           = &helmet_ubo;
    helmet_ubo_write.dstSet                = helmet_dset;

    VkWriteDescriptorSet& imgui_write = writes[3];
    imgui_write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    imgui_write.dstBinding            = 0;
    imgui_write.dstArrayElement       = 0;
    imgui_write.descriptorType        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    imgui_write.descriptorCount       = 1;
    imgui_write.pImageInfo            = &imgui_image;
    imgui_write.dstSet                = imgui_dset;

    vkUpdateDescriptorSets(engine.generic_handles.device, SDL_arraysize(writes), writes, 0, nullptr);
  }

  {
    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      rig_skinning_matrices_ubo_offsets[swapchain_image_idx] = engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));
    }

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      fig_skinning_matrices_ubo_offsets[swapchain_image_idx] = engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));
    }

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      monster_skinning_matrices_ubo_offsets[swapchain_image_idx] =
          engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));
    }

    VkDescriptorBufferInfo ubo_infos[SWAPCHAIN_IMAGES_COUNT]{};
    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      VkDescriptorBufferInfo& ubo_info = ubo_infos[swapchain_image_idx];

      ubo_info.buffer = engine.ubo_host_visible.buffer;
      ubo_info.offset = rig_skinning_matrices_ubo_offsets[swapchain_image_idx];
      ubo_info.range  = 64 * sizeof(mat4x4);
    }

    VkWriteDescriptorSet writes[SWAPCHAIN_IMAGES_COUNT]{};
    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      VkWriteDescriptorSet& write = writes[swapchain_image_idx];

      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstBinding      = 9;
      write.dstArrayElement = 0;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.descriptorCount = 1;
      write.pBufferInfo     = &ubo_infos[swapchain_image_idx];
      write.dstSet          = rig_dsets[swapchain_image_idx];
    }

    vkUpdateDescriptorSets(engine.generic_handles.device, SDL_arraysize(writes), writes, 0, nullptr);

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      ubo_infos[swapchain_image_idx].offset = fig_skinning_matrices_ubo_offsets[swapchain_image_idx];
    }

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      writes[swapchain_image_idx].dstSet = fig_dsets[swapchain_image_idx];
    }

    vkUpdateDescriptorSets(engine.generic_handles.device, SDL_arraysize(writes), writes, 0, nullptr);

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      ubo_infos[swapchain_image_idx].offset = monster_skinning_matrices_ubo_offsets[swapchain_image_idx];
    }

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      writes[swapchain_image_idx].dstSet = monster_dsets[swapchain_image_idx];
    }

    vkUpdateDescriptorSets(engine.generic_handles.device, SDL_arraysize(writes), writes, 0, nullptr);
  }

  helmet_translation[0] = -1.0f;
  helmet_translation[1] = 1.0f;
  helmet_translation[2] = 3.0f;

  robot_position[0] = 2.0f;
  robot_position[1] = 2.5f;
  robot_position[2] = 3.0f;

  rigged_position[0] = 2.0f;
  rigged_position[1] = 0.0f;
  rigged_position[2] = 3.0f;

  {
    LightSource& red = light_sources[0];
    red.setPosition(-2.0, 0.0, 1.0);
    red.setColor(2.0, 0.0, 0.0);

    LightSource& green = light_sources[1];
    green.setPosition(0.0, 0.0, 1.0);
    green.setColor(0.0, 0.0, 2.0);

    LightSource& blue = light_sources[2];
    blue.setPosition(-2.0, 2.0, 1.0);
    blue.setColor(0.0, 0.0, 2.0);

    LightSource& white = light_sources[3];
    white.setPosition(0.0, 2.0, 1.0);
    white.setColor(2.0, 0.0, 0.0);

    light_sources_count = 4;
  }

  {
    struct LightSourceAtShader
    {
      float position[4];
      float color[4];
    };

    LightSourceAtShader* dst = nullptr;
    vkMapMemory(engine.generic_handles.device, engine.ubo_host_visible.memory, lights_ubo_offset,
                SDL_arraysize(light_sources) * sizeof(LightSourceAtShader), 0, (void**)(&dst));
    for (int i = 0; i < 10; ++i)
    {
      SDL_memcpy(dst[i].position, light_sources[i].position, 3 * sizeof(float));
      SDL_memcpy(dst[i].color, light_sources[i].color, 3 * sizeof(float));
    }
    vkUnmapMemory(engine.generic_handles.device, engine.ubo_host_visible.memory);
  }

  float extent_width        = static_cast<float>(engine.generic_handles.extent2D.width);
  float extent_height       = static_cast<float>(engine.generic_handles.extent2D.height);
  float aspect_ratio        = extent_width / extent_height;
  float fov                 = to_rad(90.0f);
  float near_clipping_plane = 0.1f;
  float far_clipping_plane  = 1000.0f;
  mat4x4_perspective(projection, fov, aspect_ratio, near_clipping_plane, far_clipping_plane);

  VrLevelLoadResult result = level_generator_vr(&engine);

  vr_level_vertex_buffer_offset = result.level_load_data.vertex_target_offset;
  vr_level_index_buffer_offset  = result.level_load_data.index_target_offset;
  vr_level_index_type           = result.level_load_data.index_type;
  vr_level_index_count          = result.level_load_data.index_count;

  utility::copy<float, 2>(vr_level_entry, result.entrance_point);
  utility::copy<float, 2>(vr_level_goal, result.target_goal);

  vr_level_entry[0] *= 15.0f;
  vr_level_entry[1] *= 15.0f;

  vr_level_goal[0] *= 15.0f;
  vr_level_goal[1] *= 15.0f;

  player_position[0] = vr_level_entry[0];
  player_position[1] = 2.0f;
  player_position[2] = vr_level_entry[1];
  quat_identity(player_orientation);

  player_acceleration[0] = 0.0f;
  player_acceleration[1] = 0.0f;
  player_acceleration[2] = 0.0f;

  player_velocity[0] = 0.0f;
  player_velocity[1] = 0.0f;
  player_velocity[2] = 0.0f;
}

void Game::teardown(Engine&)
{
  for (SDL_Cursor* cursor : debug_gui.mousecursors)
    SDL_FreeCursor(cursor);
}

void Game::update(Engine& engine, float current_time_sec)
{
  uint64_t start_function_ticks = SDL_GetPerformanceCounter();

  ImGuiIO& io = ImGui::GetIO();

  // Event dispatching
  {
    SDL_Event event = {};
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
      case SDL_MOUSEWHEEL:
      {
        bool scroll_up   = event.wheel.y > 0.0;
        bool scroll_down = event.wheel.y < 0.0;

        if (scroll_up)
        {
          io.MouseWheel = 1.0f;
        }
        else if (scroll_down)
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
          camera_updown_angle -= (0.01f * event.motion.yrel);
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

        if (SDL_SCANCODE_W == event.key.keysym.scancode)
        {
          player_forward_pressed = (SDL_KEYDOWN == event.type);
        }
        else if (SDL_SCANCODE_S == event.key.keysym.scancode)
        {
          player_back_pressed = (SDL_KEYDOWN == event.type);
        }
        else if (SDL_SCANCODE_A == event.key.keysym.scancode)
        {
          player_strafe_left_pressed = (SDL_KEYDOWN == event.type);
        }
        else if (SDL_SCANCODE_D == event.key.keysym.scancode)
        {
          player_strafe_right_pressed = (SDL_KEYDOWN == event.type);
        }
        else if ((SDL_SCANCODE_F1 == event.key.keysym.scancode) and (SDL_KEYDOWN == event.type))
        {
          SDL_SetRelativeMouseMode(SDL_TRUE);
        }
        else if ((SDL_SCANCODE_F2 == event.key.keysym.scancode) and (SDL_KEYDOWN == event.type))
        {
          SDL_SetRelativeMouseMode(SDL_FALSE);
        }
      }

      break;

      default:
        break;
      }
    }
  }

  {
    SDL_Window* window = engine.generic_handles.window;
    int         w, h;
    SDL_GetWindowSize(window, &w, &h);

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    int    mx, my;
    Uint32 mouseMask               = SDL_GetMouseState(&mx, &my);
    bool   is_mouse_in_window_area = 0 < (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS);
    io.MousePos = is_mouse_in_window_area ? ImVec2((float)mx, (float)my) : ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = debug_gui.mousepressed[0] || (mouseMask & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    io.MouseDown[1] = debug_gui.mousepressed[1] || (mouseMask & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    io.MouseDown[2] = debug_gui.mousepressed[2] || (mouseMask & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;

    for (bool& iter : debug_gui.mousepressed)
      iter = false;

    if ((SDL_GetWindowFlags(window) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_MOUSE_CAPTURE)) != 0)
      io.MousePos = ImVec2((float)mx, (float)my);
    bool any_mouse_button_down = false;
    for (int n = 0; n < IM_ARRAYSIZE(io.MouseDown); n++)
      any_mouse_button_down |= io.MouseDown[n];
    if (any_mouse_button_down && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE) == 0)
      SDL_CaptureMouse(SDL_TRUE);
    if (!any_mouse_button_down && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_CAPTURE) != 0)
      SDL_CaptureMouse(SDL_FALSE);

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || ImGuiMouseCursor_None == cursor)
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

  ImGui::InputFloat3("robot position", robot_position);
  ImGui::InputFloat3("helmet position", helmet_translation);
  ImGui::InputFloat3("rigged position", rigged_position);

  ImGui::Text("%d %d | %d %d", lmb_last_cursor_position[0], lmb_last_cursor_position[1], lmb_current_cursor_position[0],
              lmb_current_cursor_position[1]);

  ImGui::Text("animation: %s, %.2f", animatedBox.animation_enabled ? "ongoing" : "stopped",
              animatedBox.animation_enabled ? current_time_sec - animatedBox.animation_start_time : 0.0f);

  if (ImGui::Button("restart cube animation"))
  {
    animatedBox.animation_enabled    = true;
    animatedBox.animation_start_time = current_time_sec;

    for (quat& rotation : animatedBox.animation_rotations)
    {
      quat_identity(rotation);
    }

    for (vec3& translation : animatedBox.animation_translations)
    {
      for (int i = 0; i < 4; ++i)
      {
        translation[i] = 0.0f;
      }
    }
  }

  ImGui::Text("animation: %s, %.2f", riggedSimple.animation_enabled ? "ongoing" : "stopped",
              riggedSimple.animation_enabled ? current_time_sec - riggedSimple.animation_start_time : 0.0f);

  if (ImGui::Button("restart rigged animation"))
  {
    riggedSimple.animation_enabled    = true;
    riggedSimple.animation_start_time = current_time_sec;

    for (quat& rotation : riggedSimple.animation_rotations)
    {
      quat_identity(rotation);
    }

    for (vec3& translation : riggedSimple.animation_translations)
    {
      for (int i = 0; i < 4; ++i)
      {
        translation[i] = 0.0f;
      }
    }
  }

  ImGui::Text("animation: %s, %.2f", riggedFigure.animation_enabled ? "ongoing" : "stopped",
              riggedFigure.animation_enabled ? current_time_sec - riggedFigure.animation_start_time : 0.0f);

  if (ImGui::Button("restart figure animation"))
  {
    riggedFigure.animation_enabled    = true;
    riggedFigure.animation_start_time = current_time_sec;

    for (quat& rotation : riggedFigure.animation_rotations)
    {
      quat_identity(rotation);
    }

    for (vec3& translation : riggedFigure.animation_translations)
    {
      for (int i = 0; i < 4; ++i)
      {
        translation[i] = 0.0f;
      }
    }
  }

  ImGui::Text("animation: %s, %.2f", monster.animation_enabled ? "ongoing" : "stopped",
              monster.animation_enabled ? current_time_sec - monster.animation_start_time : 0.0f);

  if (ImGui::Button("monster animation"))
  {
    monster.animation_enabled    = true;
    monster.animation_start_time = current_time_sec;

    for (quat& rotation : monster.animation_rotations)
    {
      quat_identity(rotation);
    }

    for (vec3& translation : monster.animation_translations)
    {
      for (int i = 0; i < 4; ++i)
      {
        translation[i] = 0.0f;
      }
    }
  }

  float avg_time = 0.0f;
  for (float time : update_times)
    avg_time += time;
  avg_time /= SDL_arraysize(update_times);

  ImGui::Text("Average update time: %f", avg_time);

  for (float time : render_times)
    avg_time += time;
  avg_time /= SDL_arraysize(render_times);

  ImGui::Text("Average render time: %f", avg_time);

  if (ImGui::Button("quit"))
  {
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
  }

  // simple animation for testing purposes
  if (animatedBox.animation_enabled)
  {
    bool  any_sampler_ongoing = false;
    float animation_time      = current_time_sec - animatedBox.animation_start_time;

    for (int anim_channel_idx = 0; anim_channel_idx < animatedBox.scene_graph.animations.data[0].channels.count;
         ++anim_channel_idx)
    {
      const AnimationChannel& channel = animatedBox.scene_graph.animations.data[0].channels.data[anim_channel_idx];
      const AnimationSampler& sampler = animatedBox.scene_graph.animations.data[0].samplers.data[channel.sampler_idx];

      if (sampler.time_frame[1] > animation_time)
      {
        any_sampler_ongoing = true;

        if (sampler.time_frame[0] < animation_time)
        {
          int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
          int   keyframe_lower         = keyframe_upper - 1;
          float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
          float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

          if (AnimationChannel::Path::Rotation == channel.target_path)
          {
            float* a = &sampler.values[4 * keyframe_lower];
            float* b = &sampler.values[4 * keyframe_upper];
            float* c = animatedBox.animation_rotations[channel.target_node_idx];
            quat_lerp(a, b, c, keyframe_uniform_time);

            animatedBox.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
          }
          else if (AnimationChannel::Path::Translation == channel.target_path)
          {
            float* a = &sampler.values[3 * keyframe_lower];
            float* b = &sampler.values[3 * keyframe_upper];
            float* c = animatedBox.animation_translations[channel.target_node_idx];
            lerp(a, b, c, 3, keyframe_uniform_time);

            animatedBox.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
          }
        }
      }
    }

    if (not any_sampler_ongoing)
    {
      animatedBox.animation_enabled = false;
    }
  }

  // simple skinning animation for testing purposes
  if (riggedSimple.animation_enabled)
  {
    bool  any_sampler_ongoing = false;
    float animation_time      = current_time_sec - riggedSimple.animation_start_time;

    for (int anim_channel_idx = 0; anim_channel_idx < riggedSimple.scene_graph.animations.data[0].channels.count;
         ++anim_channel_idx)
    // for (int anim_channel_idx = 3; anim_channel_idx < 6; ++anim_channel_idx)
    {
      const AnimationChannel& channel = riggedSimple.scene_graph.animations.data[0].channels.data[anim_channel_idx];
      const AnimationSampler& sampler = riggedSimple.scene_graph.animations.data[0].samplers.data[channel.sampler_idx];

      if (sampler.time_frame[1] > animation_time)
      {
        any_sampler_ongoing = true;

        if (sampler.time_frame[0] < animation_time)
        {
          int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
          int   keyframe_lower         = keyframe_upper - 1;
          float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
          float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

          if (AnimationChannel::Path::Rotation == channel.target_path)
          {
            float* a = &sampler.values[4 * keyframe_lower];
            float* b = &sampler.values[4 * keyframe_upper];
            float* c = riggedSimple.animation_rotations[channel.target_node_idx];
            quat_lerp(a, b, c, keyframe_uniform_time);

            riggedSimple.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
          }
          else if (AnimationChannel::Path::Translation == channel.target_path)
          {
            float* a = &sampler.values[3 * keyframe_lower];
            float* b = &sampler.values[3 * keyframe_upper];
            float* c = riggedSimple.animation_translations[channel.target_node_idx];
            lerp(a, b, c, 3, keyframe_uniform_time);

            riggedSimple.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
          }
        }
      }
    }

    if (not any_sampler_ongoing)
    {
      riggedSimple.animation_enabled = false;
      SDL_memset(riggedSimple.animation_properties, 0, sizeof(riggedSimple.animation_properties));
    }
  }

  if (riggedFigure.animation_enabled)
  {
    bool  any_sampler_ongoing = false;
    float animation_time      = current_time_sec - riggedFigure.animation_start_time;

    for (int anim_channel_idx = 0; anim_channel_idx < riggedFigure.scene_graph.animations.data[0].channels.count;
         ++anim_channel_idx)
    {
      const AnimationChannel& channel = riggedFigure.scene_graph.animations.data[0].channels.data[anim_channel_idx];
      const AnimationSampler& sampler = riggedFigure.scene_graph.animations.data[0].samplers.data[channel.sampler_idx];

      if (sampler.time_frame[1] > animation_time)
      {
        any_sampler_ongoing = true;

        if (sampler.time_frame[0] < animation_time)
        {
          int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
          int   keyframe_lower         = keyframe_upper - 1;
          float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
          float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

          if (AnimationChannel::Path::Rotation == channel.target_path)
          {
            float* a = &sampler.values[4 * keyframe_lower];
            float* b = &sampler.values[4 * keyframe_upper];
            float* c = riggedFigure.animation_rotations[channel.target_node_idx];
            quat_lerp(a, b, c, keyframe_uniform_time);

            riggedFigure.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
          }
          else if (AnimationChannel::Path::Translation == channel.target_path)
          {
            float* a = &sampler.values[3 * keyframe_lower];
            float* b = &sampler.values[3 * keyframe_upper];
            float* c = riggedFigure.animation_translations[channel.target_node_idx];
            lerp(a, b, c, 3, keyframe_uniform_time);

            riggedFigure.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
          }
        }
      }
    }

    if (not any_sampler_ongoing)
    {
      riggedFigure.animation_enabled = false;
      SDL_memset(riggedFigure.animation_properties, 0, sizeof(riggedFigure.animation_properties));
    }
  }

  if (monster.animation_enabled)
  {
    bool  any_sampler_ongoing = false;
    float animation_time      = current_time_sec - monster.animation_start_time;

    for (int anim_channel_idx = 0; anim_channel_idx < riggedFigure.scene_graph.animations.data[0].channels.count;
         ++anim_channel_idx)
    {
      const AnimationChannel& channel = monster.scene_graph.animations.data[0].channels.data[anim_channel_idx];
      const AnimationSampler& sampler = monster.scene_graph.animations.data[0].samplers.data[channel.sampler_idx];

      if (sampler.time_frame[1] > animation_time)
      {
        any_sampler_ongoing = true;

        if (sampler.time_frame[0] < animation_time)
        {
          int   keyframe_upper         = find_first_higher(sampler.times, animation_time);
          int   keyframe_lower         = keyframe_upper - 1;
          float time_between_keyframes = sampler.times[keyframe_upper] - sampler.times[keyframe_lower];
          float keyframe_uniform_time  = (animation_time - sampler.times[keyframe_lower]) / time_between_keyframes;

          if (AnimationChannel::Path::Rotation == channel.target_path)
          {
            float* a = &sampler.values[4 * keyframe_lower];
            float* b = &sampler.values[4 * keyframe_upper];
            float* c = monster.animation_rotations[channel.target_node_idx];
            quat_lerp(a, b, c, keyframe_uniform_time);

            monster.animation_properties[channel.target_node_idx] |= Node::Property::Rotation;
          }
          else if (AnimationChannel::Path::Translation == channel.target_path)
          {
            float* a = &sampler.values[3 * keyframe_lower];
            float* b = &sampler.values[3 * keyframe_upper];
            float* c = monster.animation_translations[channel.target_node_idx];
            lerp(a, b, c, 3, keyframe_uniform_time);

            monster.animation_properties[channel.target_node_idx] |= Node::Property::Translation;
          }
        }
      }
    }

    if (not any_sampler_ongoing)
    {
      monster.animation_enabled = false;
      SDL_memset(monster.animation_properties, 0, sizeof(monster.animation_properties));
    }
  }

  uint64_t end_function_ticks = SDL_GetPerformanceCounter();
  uint64_t ticks_elapsed      = end_function_ticks - start_function_ticks;

  for (int i = 0; i < (static_cast<int>(SDL_arraysize(update_times)) - 1); ++i)
  {
    update_times[i] = update_times[i + 1];
  }

  int last_element           = SDL_arraysize(update_times) - 1;
  update_times[last_element] = (float)ticks_elapsed / (float)SDL_GetPerformanceFrequency();

  if (player_forward_pressed)
  {
    float step         = 0.4f;
    player_position[0] = player_position[0] + SDL_sinf(camera_angle - M_PI_2) * step;
    player_position[2] = player_position[2] + SDL_cosf(camera_angle - M_PI_2) * step;
  }
  else if (player_back_pressed)
  {
    float step         = 0.4f;
    player_position[0] = player_position[0] + SDL_sinf(camera_angle + M_PI_2) * step;
    player_position[2] = player_position[2] + SDL_cosf(camera_angle + M_PI_2) * step;
  }

  if (player_strafe_left_pressed)
  {
    float step         = 0.4f;
    player_position[0] = player_position[0] + SDL_sinf(camera_angle) * step;
    player_position[2] = player_position[2] + SDL_cosf(camera_angle) * step;
  }
  else if (player_strafe_right_pressed)
  {
    float step         = 0.4f;
    player_position[0] = player_position[0] + SDL_sinf(camera_angle + M_PI) * step;
    player_position[2] = player_position[2] + SDL_cosf(camera_angle + M_PI) * step;
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
}

void Game::render(Engine& engine, float current_time_sec)
{
  uint64_t                 start_function_ticks = SDL_GetPerformanceCounter();
  Engine::SimpleRendering& renderer             = engine.simple_rendering;

  uint32_t image_index = 0;
  vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                        engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
  vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::Skybox];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::Skybox;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    struct VertPush
    {
      mat4x4 projection;
      mat4x4 view;
    } vertpush{};

    mat4x4_dup(vertpush.projection, projection);
    mat4x4_dup(vertpush.view, view);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::Skybox]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Skybox], 0, 1, &skybox_dset, 0,
                            nullptr);
    vkCmdPushConstants(cmd, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Skybox],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VertPush), &vertpush);
    box.renderRaw(engine, cmd);

    vkEndCommandBuffer(cmd);
  }

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::Scene3D];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::Scene3D;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::Scene3D]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Passes::Scene3D], 0, 1, &helmet_dset, 0,
                            nullptr);

    gltf::MVP push_const{};

    mat4x4_dup(push_const.projection, projection);
    mat4x4_dup(push_const.view, view);

    for (int i = 0; i < 3; ++i)
      push_const.camera_position[i] = camera_position[i];

    mat4x4_identity(push_const.model);
    mat4x4_translate(push_const.model, vr_level_goal[0], 0.0f, vr_level_goal[1]);
    // mat4x4_rotate_Y(push_const.model, push_const.model, SDL_sinf(current_time_sec * 0.3f));
    mat4x4_rotate_X(push_const.model, push_const.model, -to_rad(90.0));
    mat4x4_scale_aniso(push_const.model, push_const.model, 1.6f, 1.6f, 1.6f);
    helmet.render(engine, cmd, push_const);

    vkEndCommandBuffer(cmd);
  }

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::ColoredGeometry];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::ColoredGeometry;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::ColoredGeometry]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry], 0, 1,
                            &helmet_dset, 0, nullptr);

    gltf::MVP push_const{};

    mat4x4_dup(push_const.projection, projection);
    mat4x4_dup(push_const.view, view);

    for (int i = 0; i < light_sources_count; ++i)
    {
      quat orientation = {};
      quat_identity(orientation);

      {
        quat a    = {};
        vec3 axis = {1.0, 0.0, 0.0};
        quat_rotate(a, to_rad(60.0 * current_time_sec), axis);

        quat b     = {};
        vec3 axis2 = {0.0, 1.0, 0.0};
        quat_rotate(b, to_rad(280.0f * current_time_sec), axis2);

        quat ab = {};
        quat_mul(ab, b, a);

        quat c     = {};
        vec3 axis3 = {0.0, 0.0, 1.0};
        quat_rotate(c, to_rad(100.0f * current_time_sec), axis3);
        quat_mul(orientation, c, ab);
      }

      vec3 scale = {0.05f, 0.05f, 0.05f};
      box.renderColored(engine, cmd, push_const.projection, push_const.view, light_sources[i].position, orientation,
                        scale, light_sources[i].color, Engine::SimpleRendering::Passes::ColoredGeometry, 0);
    }

    {
      quat orientation = {};
      quat_identity(orientation);

      {
        quat a    = {};
        vec3 axis = {1.0, 0.0, 0.0};
        quat_rotate(a, to_rad(90.0f * current_time_sec / 20.0f), axis);

        quat b     = {};
        vec3 axis2 = {0.0, 1.0, 0.0};
        quat_rotate(b, to_rad(140.0f * current_time_sec / 30.0f), axis2);

        quat ab = {};
        quat_mul(ab, b, a);

        quat c     = {};
        vec3 axis3 = {0.0, 0.0, 1.0};
        quat_rotate(c, to_rad(90.0f * current_time_sec / 90.0f), axis3);
        quat_mul(orientation, c, ab);
      }

      vec3 scale = {1.0f, 1.0f, 1.0f};
      vec3 color = {0.0, 1.0, 0.0};
      animatedBox.renderColored(engine, cmd, push_const.projection, push_const.view, robot_position, orientation, scale,
                                color, Engine::SimpleRendering::Passes::ColoredGeometry, 0);
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
      mat4x4_scale_aniso(scale_matrix, scale_matrix, 15.0f, 15.0f, 15.0f);

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
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

      vkCmdDrawIndexed(cmd, static_cast<uint32_t>(vr_level_index_count), 1, 0, 0, 0);
    }

    vkEndCommandBuffer(cmd);
  }

  {
    VkCommandBuffer cmd = renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                                             Engine::SimpleRendering::Passes::ColoredGeometrySkinned];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::ColoredGeometrySkinned;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(cmd, &begin);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::ColoredGeometrySkinned]);

    gltf::MVP push_const{};
    mat4x4_dup(push_const.projection, projection);
    mat4x4_dup(push_const.view, view);

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &rig_dsets[image_index], 0, nullptr);

      quat orientation = {};
      vec3 axis        = {1.0, 0.0, 0.0};
      quat_rotate(orientation, to_rad(90.0f), axis);

      vec3 scale = {0.5f, 0.5f, 0.5f};
      vec3 color = {0.0, 0.0, 1.0};
      riggedSimple.renderColored(engine, cmd, push_const.projection, push_const.view, rigged_position, orientation,
                                 scale, color, Engine::SimpleRendering::Passes::ColoredGeometrySkinned,
                                 rig_skinning_matrices_ubo_offsets[image_index]);
    }

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &fig_dsets[image_index], 0, nullptr);

      quat orientation{};

      {
        quat standing_pose_orientation{};
        {
          vec3 axis = {1.0, 0.0, 0.0};
          quat_rotate(standing_pose_orientation, to_rad(90.0f), axis);
        }

        quat rotate_back_orientation{};
        {
          vec3  axis     = {0.0, 0.0, 1.0};
          float rotation = to_rad(90.0f);
          if (player_position[0] < camera_position[0])
            rotation = -rotation;
          quat_rotate(rotate_back_orientation, rotation, axis);
        }

        quat camera_orientation{};
        {
          float x_delta = player_position[0] - camera_position[0];
          float z_delta = player_position[2] - camera_position[2];
          vec3  axis    = {0.0, 0.0, 1.0};
          quat_rotate(camera_orientation, static_cast<float>(SDL_atan(z_delta / x_delta)), axis);
        }

        quat tmp{};
        quat_mul(tmp, standing_pose_orientation, rotate_back_orientation);
        quat_mul(orientation, tmp, camera_orientation);
      }

      vec3 scale = {1.0f, 1.0f, 1.0f};
      vec3 color = {1.0, 0.0, 0.0};

      riggedFigure.renderColored(engine, cmd, push_const.projection, push_const.view, player_position, orientation,
                                 scale, color, Engine::SimpleRendering::Passes::ColoredGeometrySkinned,
                                 fig_skinning_matrices_ubo_offsets[image_index]);
    }

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &monster_dsets[image_index], 0, nullptr);

      quat orientation = {};
      vec3 axis        = {1.0, 0.0, 0.0};
      quat_rotate(orientation, to_rad(90.0f), axis);

      vec3 scale    = {0.02f, 0.02f, 0.02f};
      vec3 color    = {1.0, 1.0, 1.0};
      vec3 position = {1.5f, -0.2f, 1.0f};

      monster.renderColored(engine, cmd, push_const.projection, push_const.view, position, orientation, scale, color,
                            Engine::SimpleRendering::Passes::ColoredGeometrySkinned,
                            monster_skinning_matrices_ubo_offsets[image_index]);
    }

    vkEndCommandBuffer(cmd);
  }

  {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGuiIO&    io        = ImGui::GetIO();

    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    SDL_assert(DebugGui::VERTEX_BUFFER_CAPACITY_BYTES >= vertex_size);
    SDL_assert(DebugGui::INDEX_BUFFER_CAPACITY_BYTES >= index_size);

    {
      ImDrawVert* vtx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.vertex_buffer_offsets[image_index], vertex_size, 0, (void**)(&vtx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx_dst += cmd_list->VtxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    {
      ImDrawIdx* idx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.index_buffer_offsets[image_index], index_size, 0, (void**)(&idx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    VkCommandBuffer command_buffer =
        renderer.secondary_command_buffers[Engine::SimpleRendering::Passes::Count * image_index +
                                           Engine::SimpleRendering::Passes::ImGui];

    {
      VkCommandBufferInheritanceInfo inheritance{};
      inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritance.renderPass           = renderer.render_pass;
      inheritance.subpass              = Engine::SimpleRendering::Passes::ImGui;
      inheritance.framebuffer          = renderer.framebuffers[image_index];
      inheritance.occlusionQueryEnable = VK_FALSE;

      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
      begin.pInheritanceInfo = &inheritance;
      vkBeginCommandBuffer(command_buffer, &begin);
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::ImGui]);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layouts[1], 0, 1,
                            &imgui_dset, 0, nullptr);

    vkCmdBindIndexBuffer(command_buffer, engine.gpu_host_visible.buffer, debug_gui.index_buffer_offsets[image_index],
                         VK_INDEX_TYPE_UINT16);

    vkCmdBindVertexBuffers(command_buffer, 0, 1, &engine.gpu_host_visible.buffer,
                           &debug_gui.vertex_buffer_offsets[image_index]);

    {
      VkViewport viewport{};
      viewport.width    = io.DisplaySize.x;
      viewport.height   = io.DisplaySize.y;
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    }

    float scale[]     = {2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y};
    float translate[] = {-1.0f, -1.0f};

    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, scale);
    vkCmdPushConstants(command_buffer, renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ImGui],
                       VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

    {
      ImDrawData* draw_data = ImGui::GetDrawData();

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
              VkRect2D scissor{};
              scissor.offset.x      = (int32_t)(pcmd->ClipRect.x) > 0 ? (int32_t)(pcmd->ClipRect.x) : 0;
              scissor.offset.y      = (int32_t)(pcmd->ClipRect.y) > 0 ? (int32_t)(pcmd->ClipRect.y) : 0;
              scissor.extent.width  = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
              scissor.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // FIXME: Why +1 here?
              vkCmdSetScissor(command_buffer, 0, 1, &scissor);
            }
            vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
          }
          idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
      }
    }

    vkEndCommandBuffer(command_buffer);
  }

  engine.submit_simple_rendering(image_index);

  uint64_t end_function_ticks = SDL_GetPerformanceCounter();
  uint64_t ticks_elapsed      = end_function_ticks - start_function_ticks;

  for (int i = 0; i < (static_cast<int>(SDL_arraysize(render_times)) - 1); ++i)
  {
    render_times[i] = render_times[i + 1];
  }

  int last_element           = SDL_arraysize(render_times) - 1;
  render_times[last_element] = (float)ticks_elapsed / (float)SDL_GetPerformanceFrequency();
}
