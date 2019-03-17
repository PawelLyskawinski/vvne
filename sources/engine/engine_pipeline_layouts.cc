#include "engine.hh"
#include "linmath.h"

namespace {

void shadowmap(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.shadow_pass,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.shadowmap.layout);
}

void skybox(Engine& engine)
{
  VkPushConstantRange ranges[] = {
      {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .offset     = 0,
          .size       = 2 * sizeof(mat4x4),
      },
  };

  VkPipelineLayoutCreateInfo ci = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = 1,
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.skybox.layout);
}

void scene3D(Engine& engine)
{
  VkPushConstantRange ranges[] = {
      {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset     = 0,
          .size       = 3 * sizeof(mat4x4) + sizeof(vec3),
      },
  };

  VkDescriptorSetLayout descriptor_sets[] = {
      engine.descriptor_set_layouts.pbr_metallic_workflow_material, //
      engine.descriptor_set_layouts.pbr_ibl_cubemaps_and_brdf_lut,
      engine.descriptor_set_layouts.single_texture_in_frag,
      engine.descriptor_set_layouts.pbr_dynamic_lights,
      engine.descriptor_set_layouts.cascade_shadow_map_matrices_ubo_frag,
  };

  VkPipelineLayoutCreateInfo ci = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = SDL_arraysize(descriptor_sets),
      .pSetLayouts            = descriptor_sets,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.scene3D.layout);
}

void pbr_water(Engine& engine)
{
  VkPushConstantRange ranges[] = {
      {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset     = 0,
          .size       = 3 * sizeof(mat4x4) + sizeof(vec3) + sizeof(float),
      },
  };

  VkDescriptorSetLayout descriptor_sets[] = {
      engine.descriptor_set_layouts.pbr_ibl_cubemaps_and_brdf_lut,
      engine.descriptor_set_layouts.pbr_dynamic_lights,
      engine.descriptor_set_layouts.single_texture_in_frag,
  };

  VkPipelineLayoutCreateInfo ci = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = SDL_arraysize(descriptor_sets),
      .pSetLayouts            = descriptor_sets,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.pbr_water.layout);
}

void colored_geometry(Engine& engine)
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

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.colored_geometry.layout);
}

void colored_geometry_triangle_strip(Engine& engine)
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

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.colored_geometry_triangle_strip.layout);
}

void colored_geometry_skinned(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.skinning_matrices,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.colored_geometry_skinned.layout);
}

void green_gui(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui.layout);
}

void green_gui_weapon_selector_box_left(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_weapon_selector_box_left.layout);
}

void green_gui_weapon_selector_box_right(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_weapon_selector_box_right.layout);
}

void green_gui_lines(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_lines.layout);
}

void green_gui_sdf_font(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_sdf_font.layout);
}

void green_gui_triangle(Engine& engine)
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

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_triangle.layout);
}

void green_gui_radar_dots(Engine& engine)
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

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.green_gui_radar_dots.layout);
}

void imgui(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.imgui.layout);
}

void debug_shadowmap_billboard(Engine& engine)
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
      .pSetLayouts            = &engine.descriptor_set_layouts.single_texture_in_frag,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.debug_billboard.layout);
}

void colored_model_wireframe(Engine& engine)
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
          .size       = sizeof(vec3),
      }
  };

  VkPipelineLayoutCreateInfo ci = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = SDL_arraysize(ranges),
      .pPushConstantRanges    = ranges,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &engine.pipelines.colored_model_wireframe.layout);
}

} // namespace

void Engine::setup_pipeline_layouts()
{
  shadowmap(*this);
  skybox(*this);
  scene3D(*this);
  pbr_water(*this);
  colored_geometry(*this);
  colored_geometry_triangle_strip(*this);
  colored_geometry_skinned(*this);
  green_gui(*this);
  green_gui_weapon_selector_box_left(*this);
  green_gui_weapon_selector_box_right(*this);
  green_gui_lines(*this);
  green_gui_sdf_font(*this);
  green_gui_triangle(*this);
  green_gui_radar_dots(*this);
  imgui(*this);
  debug_shadowmap_billboard(*this);
  colored_model_wireframe(*this);
}