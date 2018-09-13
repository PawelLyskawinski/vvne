#include "engine.hh"
#include "linmath.h"

void Engine::setup_pipeline_layouts()
{
  // ---------------------------------------------------------------------------
  // SHADOWMAP
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4) + sizeof(uint32_t),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &shadow_pass_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &shadowmap_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // SKYBOX
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 2 * sizeof(mat4x4),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {single_texture_in_frag_descriptor_set_layout};

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &skybox_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // SCENE3D
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 3 * sizeof(mat4x4) + sizeof(vec3),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {
        pbr_metallic_workflow_material_descriptor_set_layout, //
        pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout,
        single_texture_in_frag_descriptor_set_layout,
        pbr_dynamic_lights_descriptor_set_layout,
        cascade_shadow_map_matrices_ubo_frag_set_layout,
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &scene3D_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // PBR WATER
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 3 * sizeof(mat4x4) + sizeof(vec3) + sizeof(float),
        },
    };

    VkDescriptorSetLayout descriptor_sets[] = {
        pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout,
        pbr_dynamic_lights_descriptor_set_layout,
        single_texture_in_frag_descriptor_set_layout,
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = SDL_arraysize(descriptor_sets),
        .pSetLayouts            = descriptor_sets,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &pbr_water_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // COLORED GEOMETRY
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {

            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &colored_geometry_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // COLORED GEOMETRY TRIANGLE STRIP
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {

            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &colored_geometry_triangle_strip_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // COLORED GEOMETRY SKINNED
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &skinning_matrices_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &colored_geometry_skinned_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI WEAPON SELECTOR BOX LEFT
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_weapon_selector_box_left_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI WEAPON SELECTOR BOX RIGHT
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = 3 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_weapon_selector_box_right_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI LINES
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = sizeof(vec4),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_lines_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI SDF FONT
  // ---------------------------------------------------------------------------
  {
    struct VertexPushConstant
    {
      mat4x4 mvp;
      vec2   character_coordinate;
      vec2   character_size;
    };

    struct FragmentPushConstant
    {
      vec3  color;
      float time;
    };

    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(VertexPushConstant),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(VertexPushConstant),
            .size       = sizeof(FragmentPushConstant),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_sdf_font_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI TRIANGLE
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 2 * sizeof(vec4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 2 * sizeof(vec4),
            .size       = sizeof(vec4),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_triangle_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // GREEN GUI RADAR DOTS
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(vec4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(vec4),
            .size       = sizeof(vec4),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &green_gui_radar_dots_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // IMGUI
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = 16 * sizeof(float),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &imgui_pipeline_layout);
  }

  // ---------------------------------------------------------------------------
  // DEBUG SHADOWMAP BILLBOARD
  // ---------------------------------------------------------------------------
  {
    VkPushConstantRange ranges[] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(mat4x4),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof(mat4x4),
            .size       = sizeof(uint32_t),
        },
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &single_texture_in_frag_descriptor_set_layout,
        .pushConstantRangeCount = SDL_arraysize(ranges),
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(device, &ci, nullptr, &debug_billboard_pipeline_layout);
  }
}