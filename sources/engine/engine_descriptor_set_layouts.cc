#include "engine.hh"

void Engine::setup_descriptor_set_layouts()
{
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.shadow_pass);
  }

  // --------------------------------------------------------------- //
  // Metallic workflow PBR materials descriptor set layout
  //
  // texture ordering:
  // 0. albedo
  // 1. metallic roughness (r: UNUSED, b: metallness, g: roughness)
  // 2. emissive
  // 3. ambient occlusion
  // 4. normal
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 5,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.pbr_metallic_workflow_material);
  }

  // --------------------------------------------------------------- //
  // PBR IBL cubemaps and BRDF lookup table
  //
  // texture ordering:
  // 0.0 irradiance (cubemap)
  // 0.1 prefiltered (cubemap)
  // 1   BRDF lookup table (2D)
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 2,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SDL_arraysize(bindings),
        .pBindings    = bindings,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.pbr_ibl_cubemaps_and_brdf_lut);
  }

  // --------------------------------------------------------------- //
  // PBR dynamic light sources
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.pbr_dynamic_lights);
  }

  // --------------------------------------------------------------- //
  // Single texture in fragment shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.single_texture_in_frag);
  }

  // --------------------------------------------------------------- //
  // Two textures in fragment shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SDL_arraysize(bindings),
        .pBindings    = bindings,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.two_textures_in_frag);
  }

  // --------------------------------------------------------------- //
  // Skinning matrices in vertex shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.skinning_matrices);
  }

  // --------------------------------------------------------------- //
  // Light space matrix in vertex shader (shadow mapping)
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.cascade_shadow_map_matrices_ubo_frag);
  }

  // --------------------------------------------------------------- //
  // used for frustum culling in terrain tesselation control shader
  // --------------------------------------------------------------- //
  {
    VkDescriptorSetLayoutBinding binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    };

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &binding,
    };

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descriptor_set_layouts.frustum_planes);
  }
}