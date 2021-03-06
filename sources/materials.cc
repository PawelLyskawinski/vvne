#include "materials.hh"
#include "engine/cubemap.hh"
#include "game_constants.hh"
#include "imgui.h"
#include "terrain_as_a_function.hh"
#include <algorithm>

namespace {

struct ImguiFontSurface
{
  ImguiFontSurface()
  {
    ImGuiIO&       io             = ImGui::GetIO();
    unsigned char* guifont_pixels = nullptr;
    int            guifont_w      = 0;
    int            guifont_h      = 0;
    io.Fonts->GetTexDataAsRGBA32(&guifont_pixels, &guifont_w, &guifont_h);
    surface = SDL_CreateRGBSurfaceWithFormatFrom(guifont_pixels, guifont_w, guifont_h, 32, 4 * guifont_w,
                                                 SDL_PIXELFORMAT_RGBA8888);
  }

  ~ImguiFontSurface()
  {
    SDL_FreeSurface(surface);
  }

  SDL_Surface* surface;
};

struct Cursor
{
  explicit Cursor(const char* initial)
      : ptr(initial)
  {
  }

  Cursor& forward(char c)
  {
    for (; c != *ptr; ++ptr)
      ;
    ++ptr;
    return *this;
  }

  template <typename T>[[nodiscard]] T read_uint() const
  {
    return static_cast<T>(SDL_strtoul(ptr, nullptr, 10));
  }
  template <typename T>[[nodiscard]] T read_int() const
  {
    return static_cast<T>(SDL_strtol(ptr, nullptr, 10));
  }

  const char* ptr;
};

} // namespace

void LightSourcesSoA::push(const LightSource* begin, const LightSource* end)
{
  std::transform(begin, end, positions + count, [](const LightSource& it) { return it.position; });
  std::transform(begin, end, colors + count, [](const LightSource& it) { return it.color; });
  count += std::distance(begin, end);
}

void Materials::setup(Engine& engine)
{
  imgui_font_texture = engine.load_texture(ImguiFontSurface().surface);

  {
    GpuMemoryBlock& block = engine.memory_blocks.host_coherent;
    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      imgui_vertex_buffer_offsets[i] = block.allocate_aligned(IMGUI_VERTEX_BUFFER_CAPACITY_BYTES);
      imgui_index_buffer_offsets[i]  = block.allocate_aligned(IMGUI_INDEX_BUFFER_CAPACITY_BYTES);
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
  water_normal            = engine.load_texture("../assets/pbr_water/normal_map.jpg");

  const VkDeviceSize light_sources_ubo_size     = sizeof(LightSourcesSoA);
  const VkDeviceSize skinning_matrices_ubo_size = 64 * sizeof(Mat4x4);

  {
    GpuMemoryBlock& block = engine.memory_blocks.host_coherent_ubo;

    block.allocate_aligned_ranged(pbr_dynamic_lights_ubo_offsets, SDL_arraysize(pbr_dynamic_lights_ubo_offsets),
                                  light_sources_ubo_size);

    block.allocate_aligned_ranged(rig_skinning_matrices_ubo_offsets, SDL_arraysize(rig_skinning_matrices_ubo_offsets),
                                  skinning_matrices_ubo_size);

    block.allocate_aligned_ranged(fig_skinning_matrices_ubo_offsets, SDL_arraysize(fig_skinning_matrices_ubo_offsets),
                                  skinning_matrices_ubo_size);

    block.allocate_aligned_ranged(monster_skinning_matrices_ubo_offsets,
                                  SDL_arraysize(monster_skinning_matrices_ubo_offsets), skinning_matrices_ubo_size);

    block.allocate_aligned_ranged(cascade_view_proj_mat_ubo_offsets, SDL_arraysize(cascade_view_proj_mat_ubo_offsets),
                                  SHADOWMAP_CASCADE_COUNT * sizeof(Mat4x4) + sizeof(Vec4));

    block.allocate_aligned_ranged(frustum_planes_ubo_offsets, SDL_arraysize(frustum_planes_ubo_offsets),
                                  6 * sizeof(Vec4));
  }

  engine.memory_blocks.host_coherent.allocate_aligned_ranged(green_gui_rulers_buffer_offsets,
                                                             SDL_arraysize(green_gui_rulers_buffer_offsets),
                                                             MAX_ROBOT_GUI_LINES * sizeof(Vec2));

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

    image.imageView = imgui_font_texture.image_view;
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
    image.imageView = engine.shadowmap_image.image_view;
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
        .range  = SHADOWMAP_CASCADE_COUNT * sizeof(Mat4x4),
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
        .range  = SHADOWMAP_CASCADE_COUNT * sizeof(Mat4x4) + sizeof(Vec4),
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

  for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkDescriptorSetAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = engine.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &engine.descriptor_set_layouts.frustum_planes,
    };

    vkAllocateDescriptorSets(engine.device, &allocate, &frustum_planes_dset[i]);

    VkDescriptorBufferInfo ubo = {
        .buffer = engine.gpu_host_coherent_ubo_memory_buffer,
        .offset = frustum_planes_ubo_offsets[i],
        .range  = SHADOWMAP_CASCADE_COUNT * 6 * sizeof(Vec4),
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &ubo,
    };

    write.dstSet = frustum_planes_dset[i];
    vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
  }

  //
  // billboard vertex data (triangle strip topology)
  //
  {
    struct GreenGuiVertex
    {
      Vec2 position;
      Vec2 uv;
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
      Vec3 position;
      Vec3 normal;
      Vec2 tex_coord;
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

    engine.memory_blocks.host_visible_transfer_source.allocator.reset();

    VkDeviceSize vertices_host_offset =
        engine.memory_blocks.host_visible_transfer_source.allocate_aligned(sizeof(vertices));

    green_gui_billboard_vertex_buffer_offset = engine.memory_blocks.device_local.allocate_aligned(sizeof(vertices));

    {
      void* data = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory, vertices_host_offset,
                  sizeof(vertices), 0, &data);
      SDL_memcpy(data, vertices, sizeof(vertices));
      vkUnmapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory);
    }

    VkDeviceSize cg_vertices_host_offset = 0;

    cg_vertices_host_offset = engine.memory_blocks.host_visible_transfer_source.allocate_aligned(sizeof(cg_vertices));
    regular_billboard_vertex_buffer_offset = engine.memory_blocks.device_local.allocate_aligned(sizeof(cg_vertices));

    {
      void* data = nullptr;
      vkMapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory, cg_vertices_host_offset,
                  sizeof(cg_vertices), 0, &data);
      SDL_memcpy(data, cg_vertices, sizeof(cg_vertices));
      vkUnmapMemory(engine.device, engine.memory_blocks.host_visible_transfer_source.memory);
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
          .size                = sizeof(vertices),
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
    engine.memory_blocks.host_visible_transfer_source.allocator.reset();
  }

  {
    const uint32_t layers = 10;
    tesselation_instances = tesellated_patches_nonindexed_calculate_count(layers);
    tesselation_vb_offset =
        engine.memory_blocks.host_coherent.allocator.allocate_bytes(sizeof(TerrainVertex) * tesselation_instances);

    TerrainVertex* dst = nullptr;
    vkMapMemory(engine.device, engine.memory_blocks.host_coherent.memory, tesselation_vb_offset,
                sizeof(TerrainVertex) * tesselation_instances, 0, reinterpret_cast<void**>(&dst));
    tesellated_patches_nonindexed_generate(layers, 100.0f, dst);
    vkUnmapMemory(engine.device, engine.memory_blocks.host_coherent.memory);
  }

  {
    SDL_RWops* ctx              = SDL_RWFromFile("../assets/lucida_sans_sdf.fnt", "r");
    int        fnt_file_size    = static_cast<int>(SDL_RWsize(ctx));
    char*      fnt_file_content = engine.generic_allocator->allocate<char>(static_cast<uint32_t>(fnt_file_size));
    SDL_RWread(ctx, fnt_file_content, sizeof(char), static_cast<size_t>(fnt_file_size));
    SDL_RWclose(ctx);

    Cursor cursor(fnt_file_content);
    for (int i = 0; i < 4; ++i)
    {
      cursor.forward('\n');
    }

    for (unsigned i = 0; i < SDL_arraysize(lucida_sans_sdf_chars); ++i)
    {
      uint8_t& id   = lucida_sans_sdf_char_ids[i];
      SdfChar& data = lucida_sans_sdf_chars[i];

      id            = cursor.forward('=').read_uint<uint8_t>();
      data.x        = cursor.forward('=').read_uint<uint16_t>();
      data.y        = cursor.forward('=').read_uint<uint16_t>();
      data.width    = cursor.forward('=').read_uint<uint8_t>();
      data.height   = cursor.forward('=').read_uint<uint8_t>();
      data.xoffset  = cursor.forward('=').read_int<int8_t>();
      data.yoffset  = cursor.forward('=').read_int<int8_t>();
      data.xadvance = cursor.forward('=').read_uint<uint8_t>();
      cursor.forward('\n');
    }

    engine.generic_allocator->free(fnt_file_content, static_cast<uint32_t>(fnt_file_size));
  }

  pbr_light_sources_cache_lock = SDL_CreateMutex();
}

void Materials::teardown(Engine& engine)
{
  SDL_DestroyMutex(pbr_light_sources_cache_lock);
}
