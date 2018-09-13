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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &shadow_pass_descriptor_set_layout);
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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &pbr_metallic_workflow_material_descriptor_set_layout);
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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &pbr_ibl_cubemaps_and_brdf_lut_descriptor_set_layout);
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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &pbr_dynamic_lights_descriptor_set_layout);
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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &single_texture_in_frag_descriptor_set_layout);
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

    vkCreateDescriptorSetLayout(device, &ci, nullptr, &skinning_matrices_descriptor_set_layout);
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

    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    vkCreateDescriptorSetLayout(device, &ci, nullptr, &cascade_shadow_map_matrices_ubo_frag_set_layout);
  }
}