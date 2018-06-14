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

  VkCommandBuffer select(int subpass) const
  {
    return collection[Engine::SimpleRendering::Passes::Count * image_index + subpass];
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

  void rotateX(float rads)
  {
    vec3 axis = {1.0, 0.0, 0.0};
    rotate(axis, rads);
  }

  void rotateY(float rads)
  {
    vec3 axis = {0.0, 1.0, 0.0};
    rotate(axis, rads);
  }

  void rotateZ(float rads)
  {
    vec3 axis = {0.0, 0.0, 1.0};
    rotate(axis, rads);
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

float avg(float* values, int n)
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
    vkMapMemory(device, memory, offset, size, 0, (void**)(&data));
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

  {
    int cubemap_size[2]     = {512, 512};
    environment_cubemap_idx = generate_cubemap(&engine, this, "../assets/old_industrial_hall.jpg", cubemap_size);
    irradiance_cubemap_idx  = generate_irradiance_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    prefiltered_cubemap_idx = generate_prefiltered_cubemap(&engine, this, environment_cubemap_idx, cubemap_size);
    brdf_lookup_idx         = generate_brdf_lookup(&engine, cubemap_size[0]);
  }

  struct LightSource
  {
    vec3 position;
    vec3 color;
  };

  const VkDeviceSize light_sources_ubo_size = SDL_arraysize(light_source_positions) * sizeof(LightSource);
  lights_ubo_offset                         = engine.ubo_host_visible.allocate(light_sources_ubo_size);

  for (VkDeviceSize& offset : rig_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));

  for (VkDeviceSize& offset : fig_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));

  for (VkDeviceSize& offset : monster_skinning_matrices_ubo_offsets)
    offset = engine.ubo_host_visible.allocate(64 * sizeof(mat4x4));

  // ----------------------------------------------------------------------------------------------
  // Descriptor sets
  // ----------------------------------------------------------------------------------------------

  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.generic_handles.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.simple_rendering.descriptor_set_layout,
    };

    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &skybox_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &helmet_dset);
    vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &imgui_dset);

    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &rig_dsets[i]);
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &fig_dsets[i]);
      vkAllocateDescriptorSets(engine.generic_handles.device, &allocate, &monster_dsets[i]);
    }
  }

  {
    VkDescriptorImageInfo skybox_image = {
        .sampler     = engine.generic_handles.texture_sampler,
        .imageView   = engine.images.image_views[environment_cubemap_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet skybox_write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = skybox_dset,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &skybox_image,
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &skybox_write, 0, nullptr);
  }

  {
    VkDescriptorImageInfo imgui_image = {
        .sampler     = engine.generic_handles.texture_sampler,
        .imageView   = engine.images.image_views[debug_gui.font_texture_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet imgui_write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = imgui_dset,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &imgui_image,
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &imgui_write, 0, nullptr);
  }

  {
    const Material& material = helmet.scene_graph.materials.data[0];

    int ts[8] = {};

    ts[0] = material.albedo_texture_idx;
    ts[1] = material.metal_roughness_texture_idx;
    ts[2] = material.emissive_texture_idx;
    ts[3] = material.AO_texture_idx;
    ts[4] = material.normal_texture_idx;
    ts[5] = irradiance_cubemap_idx;
    ts[6] = prefiltered_cubemap_idx;
    ts[7] = brdf_lookup_idx;

    VkDescriptorImageInfo helmet_images[8] = {};

    for (unsigned i = 0; i < SDL_arraysize(helmet_images); ++i)
    {
      helmet_images[i].sampler     = engine.generic_handles.texture_sampler;
      helmet_images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      helmet_images[i].imageView   = engine.images.image_views[ts[i]];
    }

    VkWriteDescriptorSet helmet_write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = helmet_dset,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = SDL_arraysize(helmet_images),
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = helmet_images,
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &helmet_write, 0, nullptr);

    VkDescriptorBufferInfo helmet_ubo = {
        .buffer = engine.ubo_host_visible.buffer,
        .offset = lights_ubo_offset,
        .range  = light_sources_ubo_size,
    };

    VkWriteDescriptorSet helmet_ubo_write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = helmet_dset,
        .dstBinding      = 8,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &helmet_ubo,
    };

    vkUpdateDescriptorSets(engine.generic_handles.device, 1, &helmet_ubo_write, 0, nullptr);
  }

  {
    VkDescriptorBufferInfo ubo_infos[SWAPCHAIN_IMAGES_COUNT] = {
        {
            .buffer = engine.ubo_host_visible.buffer,
            .range  = 64 * sizeof(mat4x4),
        },
    };

    for (unsigned i = 1; i < SDL_arraysize(ubo_infos); ++i)
      ubo_infos[i] = ubo_infos[0];

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
      ubo_infos[swapchain_image_idx].offset = rig_skinning_matrices_ubo_offsets[swapchain_image_idx];

    VkWriteDescriptorSet writes[SWAPCHAIN_IMAGES_COUNT] = {
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding      = 9,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        },
    };

    for (unsigned i = 1; i < SDL_arraysize(writes); ++i)
      writes[i] = writes[0];

    for (int swapchain_image_idx = 0; swapchain_image_idx < SWAPCHAIN_IMAGES_COUNT; ++swapchain_image_idx)
    {
      writes[swapchain_image_idx].pBufferInfo = &ubo_infos[swapchain_image_idx];
      writes[swapchain_image_idx].dstSet      = rig_dsets[swapchain_image_idx];
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

  vec3_set(helmet_translation, -1.0f, 1.0f, 3.0f);
  vec3_set(robot_position, 2.0f, 2.5f, 3.0f);
  vec3_set(rigged_position, 2.0f, 0.0f, 3.0f);

  {
    light_sources_count = 4;

    vec3_set(light_source_positions[0], -2.0f, 0.0f, 1.0f);
    vec3_set(light_source_positions[1], 0.0f, 0.0f, 1.0f);
    vec3_set(light_source_positions[2], -2.0f, 2.0f, 1.0f);
    vec3_set(light_source_positions[3], 0.0f, 2.0f, 1.0f);

    vec3_set(light_source_colors[0], 2.0, 0.0, 0.0);
    vec3_set(light_source_colors[1], 0.0, 0.0, 2.0);
    vec3_set(light_source_colors[2], 0.0, 0.0, 2.0);
    vec3_set(light_source_colors[3], 1.0, 0.0, 0.0);
  }

  {
    ScopedMemoryMap memory_map(engine.generic_handles.device, engine.ubo_host_visible.memory, lights_ubo_offset,
                               light_sources_ubo_size);

    LightSource* dst = memory_map.get<LightSource>();
    for (int i = 0; i < 10; ++i)
    {
      SDL_memcpy(dst[i].position, light_source_positions[i], sizeof(vec3));
      SDL_memcpy(dst[i].color, light_source_colors[i], sizeof(vec3));
    }
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

    ImGuiIO& io    = ImGui::GetIO();
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

  ImGui::Text("Booster jet fluel");
  ImGui::ProgressBar(booster_jet_fuel);

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
  animate_model(riggedFigure, current_time_sec);
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

  ImGui::Text("acceleration: %.2f %.2f %.2f", player_acceleration[0], player_acceleration[1], player_acceleration[2]);
  ImGui::Text("velocity:     %.2f %.2f %.2f", player_velocity[0], player_velocity[1], player_velocity[2]);

  ImGui::Text("WASD - movement");
  ImGui::Text("F1 - enable first person view");
  ImGui::Text("F2 - disable first person view");
  ImGui::Text("ESC - exit");

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
  FunctionTimer            timer(render_times, SDL_arraysize(render_times));
  Engine::SimpleRendering& renderer = engine.simple_rendering;

  uint32_t image_index = 0;
  vkAcquireNextImageKHR(engine.generic_handles.device, engine.generic_handles.swapchain, UINT64_MAX,
                        engine.generic_handles.image_available, VK_NULL_HANDLE, &image_index);
  vkWaitForFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(engine.generic_handles.device, 1, &renderer.submition_fences[image_index]);

  CommandBufferSelector command_selector(engine.simple_rendering, image_index);
  CommandBufferStarter  command_starter(renderer.render_pass, renderer.framebuffers[image_index]);

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Passes::Skybox);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Passes::Skybox);

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
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Passes::Scene3D);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Scene3D);

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
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Passes::ColoredGeometry);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Passes::ColoredGeometry);

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
      Quaternion orientation;

      {
        Quaternion a;
        a.rotateX(to_rad(60.0f * current_time_sec));

        Quaternion b;
        b.rotateY(to_rad(280.0f * current_time_sec));

        Quaternion c;
        c.rotateZ(to_rad(100.0f * current_time_sec));

        orientation = c * b * c;
      }

      vec3 scale = {0.05f, 0.05f, 0.05f};
      box.renderColored(engine, cmd, push_const.projection, push_const.view, light_source_positions[i],
                        orientation.data(), scale, light_source_colors[i],
                        Engine::SimpleRendering::Passes::ColoredGeometry, 0);
    }

    {
      Quaternion orientation;

      {
        Quaternion a;
        a.rotateX(to_rad(90.0f * current_time_sec / 20.0f));

        Quaternion b;
        b.rotateY(to_rad(140.0f * current_time_sec / 30.0f));

        Quaternion c;
        c.rotateZ(to_rad(90.0f * current_time_sec / 90.0f));

        orientation = c * b * a;
      }

      vec3 scale = {1.0f, 1.0f, 1.0f};
      vec3 color = {0.0, 1.0, 0.0};
      animatedBox.renderColored(engine, cmd, push_const.projection, push_const.view, robot_position, orientation.data(),
                                scale, color, Engine::SimpleRendering::Passes::ColoredGeometry, 0);
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
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), color);

      vkCmdPushConstants(cmd,
                         engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometry],
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

      vkCmdDrawIndexed(cmd, static_cast<uint32_t>(vr_level_index_count), 1, 0, 0, 0);
    }
  }

  {
    VkCommandBuffer cmd       = command_selector.select(Engine::SimpleRendering::Passes::ColoredGeometrySkinned);
    ScopedCommand   cmd_scope = command_starter.begin(cmd, Engine::SimpleRendering::Passes::ColoredGeometrySkinned);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer.pipelines[Engine::SimpleRendering::Passes::ColoredGeometrySkinned]);

    gltf::MVP push_const{};
    mat4x4_dup(push_const.projection, projection);
    mat4x4_dup(push_const.view, view);

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &rig_dsets[image_index], 0, nullptr);

      Quaternion orientation;
      orientation.rotateX(to_rad(90.0f));

      vec3 scale = {0.5f, 0.5f, 0.5f};
      vec3 color = {0.0, 0.0, 1.0};
      riggedSimple.renderColored(
          engine, cmd, push_const.projection, push_const.view, rigged_position, orientation.data(), scale, color,
          Engine::SimpleRendering::Passes::ColoredGeometrySkinned, rig_skinning_matrices_ubo_offsets[image_index]);
    }

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &fig_dsets[image_index], 0, nullptr);

      Quaternion orientation;

      {
        Quaternion standing_pose;
        standing_pose.rotateX(to_rad(90.0f));

        Quaternion rotate_back;
        rotate_back.rotateZ(player_position[0] < camera_position[0] ? -to_rad(90.0f) : to_rad(90.0f));

        float      x_delta = player_position[0] - camera_position[0];
        float      z_delta = player_position[2] - camera_position[2];
        Quaternion camera;
        camera.rotateZ(static_cast<float>(SDL_atan(z_delta / x_delta)));

        orientation = standing_pose * rotate_back * camera;
      }

      vec3 scale = {1.0f, 1.0f, 1.0f};
      vec3 color = {1.0, 0.0, 0.0};

      riggedFigure.renderColored(
          engine, cmd, push_const.projection, push_const.view, player_position, orientation.data(), scale, color,
          Engine::SimpleRendering::Passes::ColoredGeometrySkinned, fig_skinning_matrices_ubo_offsets[image_index]);
    }

    {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              renderer.pipeline_layouts[Engine::SimpleRendering::Passes::ColoredGeometrySkinned], 0, 1,
                              &monster_dsets[image_index], 0, nullptr);

      Quaternion orientation;
      orientation.rotateX(to_rad(90.0f));

      vec3 scale    = {0.02f, 0.02f, 0.02f};
      vec3 color    = {1.0, 1.0, 1.0};
      vec3 position = {1.5f, -0.2f, 1.0f};

      monster.renderColored(engine, cmd, push_const.projection, push_const.view, position, orientation.data(), scale,
                            color, Engine::SimpleRendering::Passes::ColoredGeometrySkinned,
                            monster_skinning_matrices_ubo_offsets[image_index]);
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
      ImDrawVert* vtx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.vertex_buffer_offsets[image_index], vertex_size, 0, (void**)(&vtx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        vtx_dst += cmd_list->VtxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    if (0 < index_size)
    {
      ImDrawIdx* idx_dst = nullptr;
      vkMapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory,
                  debug_gui.index_buffer_offsets[image_index], index_size, 0, (void**)(&idx_dst));

      for (int n = 0; n < draw_data->CmdListsCount; ++n)
      {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
      }
      vkUnmapMemory(engine.generic_handles.device, engine.gpu_host_visible.memory);
    }

    VkCommandBuffer command_buffer = command_selector.select(Engine::SimpleRendering::Passes::ImGui);
    ScopedCommand   cmd_scope      = command_starter.begin(command_buffer, Engine::SimpleRendering::Passes::ImGui);

    if (vertex_size and index_size)
    {

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
              vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
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
