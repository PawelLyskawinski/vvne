#include "engine.hh"
#include "linmath.h"

namespace {

struct TrianglesVertex
{
  vec3 position;
  vec3 normal;
  vec2 tex_coord;
};

struct ImguiVertex
{
  vec2     position;
  vec2     tex_coord;
  uint32_t color;
};

struct SkinnedVertex
{
  vec3     position;
  vec3     normal;
  vec2     texcoord;
  uint16_t joint[4];
  vec4     weight;
};

struct GreenGuiVertex
{
  vec2 position;
  vec2 uv;
};

struct TwoStageShader
{
  explicit TwoStageShader(Engine& engine, const char* vs_name, const char* fs_name)
      : device(engine.device)
      , shader_stages{
            {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                .module = engine.load_shader(vs_name),
                .pName  = "main",
            },
            {

                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = engine.load_shader(fs_name),
                .pName  = "main",
            },
        }
  {
  }

  ~TwoStageShader()
  {
    vkDestroyShaderModule(device, shader_stages[0].module, nullptr);
    vkDestroyShaderModule(device, shader_stages[1].module, nullptr);
  }

  VkDevice                        device;
  VkPipelineShaderStageCreateInfo shader_stages[2];
};

void shadow_mapping(Engine& engine)
{
  TwoStageShader shaders(engine, "depth_pass.vert", "depth_pass.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(SHADOWMAP_IMAGE_DIM),
          .height   = static_cast<float>(SHADOWMAP_IMAGE_DIM),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {.offset = {0, 0},
       .extent =
           {
               .width  = SHADOWMAP_IMAGE_DIM,
               .height = SHADOWMAP_IMAGE_DIM,
           }},
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_NONE,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_TRUE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.shadowmap.layout,
      .renderPass          = engine.render_passes.shadowmap.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.shadowmap.pipeline);
}

void skybox(Engine& engine)
{
  TwoStageShader shaders(engine, "skybox.vert", "skybox.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_BACK_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.skybox.layout,
      .renderPass          = engine.render_passes.skybox.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.skybox.pipeline);
}

void scene3d(Engine& engine)
{
  TwoStageShader shaders(engine, "triangle_push.vert", "triangle_push.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal)),
      },
      {
          .location = 2,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.scene3D.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.scene3D.pipeline);
}

void colored_geometry(Engine& engine)
{
  TwoStageShader shaders(engine, "colored_geometry.vert", "colored_geometry.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.colored_geometry.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.colored_geometry.pipeline);
}

void colored_geometry_triangle_strip(Engine& engine)
{
  TwoStageShader shaders(engine, "colored_geometry.vert", "colored_geometry.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.colored_geometry_triangle_strip.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.colored_geometry_triangle_strip.pipeline);
}

void colored_geometry_skinned(Engine& engine)
{
  TwoStageShader shaders(engine, "colored_geometry_skinned.vert", "colored_geometry_skinned.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, normal)),
      },
      {
          .location = 2,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, texcoord)),
      },
      {
          .location = 3,
          .binding  = 0,
          .format   = VK_FORMAT_R16G16B16A16_UINT,
          .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, joint)),
      },
      {
          .location = 4,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(SkinnedVertex, weight)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(SkinnedVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.colored_geometry_skinned.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.colored_geometry_skinned.pipeline);
}

void imgui(Engine& engine)
{
  TwoStageShader shaders(engine, "imgui.vert", "imgui.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, tex_coord)),
      },
      {
          .location = 2,
          .binding  = 0,
          .format   = VK_FORMAT_R8G8B8A8_UNORM,
          .offset   = static_cast<uint32_t>(offsetof(ImguiVertex, color)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(ImguiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_NONE,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_FALSE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_TRUE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT};

  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = SDL_arraysize(dynamic_states),
      .pDynamicStates    = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state,
      .layout              = engine.pipelines.imgui.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.imgui.pipeline);
}

void green_gui(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui.vert", "green_gui.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.green_gui.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.green_gui.pipeline);
}

void green_gui_weapon_selector_box_left(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_weapon_selector_box.vert", "green_gui_weapon_selector_box.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.green_gui_weapon_selector_box_left.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.green_gui_weapon_selector_box_left.pipeline);
}

void green_gui_weapon_selector_box_right(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_weapon_selector_box.vert", "green_gui_weapon_selector_box.frag");

  VkSpecializationMapEntry specialization_entry = {
      .constantID = 0,
      .offset     = 0,
      .size       = sizeof(bool),
  };

  bool specialization_data = false;

  VkSpecializationInfo shader_specialization_info = {
      .mapEntryCount = 1,
      .pMapEntries   = &specialization_entry,
      .dataSize      = sizeof(bool),
      .pData         = &specialization_data,
  };

  shaders.shader_stages[1].pSpecializationInfo = &shader_specialization_info;

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.green_gui_weapon_selector_box_right.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.green_gui_weapon_selector_box_right.pipeline);
} // namespace

void green_gui_lines(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_lines.vert", "green_gui_lines.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = 0,
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(vec2),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = SDL_arraysize(dynamic_states),
      .pDynamicStates    = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state_info,
      .layout              = engine.pipelines.green_gui_lines.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.green_gui_lines.pipeline);
}

void green_gui_sdf(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_sdf.vert", "green_gui_sdf.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = SDL_arraysize(dynamic_states),
      .pDynamicStates    = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state,
      .layout              = engine.pipelines.green_gui_sdf_font.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.green_gui_sdf_font.pipeline);
}

void green_gui_triangle(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_triangle.vert", "green_gui_triangle.frag");

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.green_gui_triangle.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.green_gui_triangle.pipeline);
}

void green_gui_radar_dots(Engine& engine)
{
  TwoStageShader shaders(engine, "green_gui_radar_dots.vert", "green_gui_radar_dots.frag");

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_LINE_WIDTH};

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = SDL_arraysize(dynamic_states),
      .pDynamicStates    = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state_info,
      .layout              = engine.pipelines.green_gui_radar_dots.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.green_gui_radar_dots.pipeline);
}

void pbr_water(Engine& engine)
{
  TwoStageShader shaders(engine, "pbr_water.vert", "pbr_water.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal)),
      },
      {
          .location = 2,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.pbr_water.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.pbr_water.pipeline);
}

void debug_billboard_texture_array(Engine& engine)
{
  TwoStageShader shaders(engine, "debug_billboard_texture_array.vert", "debug_billboard_texture_array.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.debug_billboard_texture_array.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.debug_billboard_texture_array.pipeline);
}

void debug_billboard(Engine& engine)
{
  TwoStageShader shaders(engine, "debug_billboard.vert", "debug_billboard.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(GreenGuiVertex, uv)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(GreenGuiVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.debug_billboard.layout,
      .renderPass          = engine.render_passes.gui.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.debug_billboard.pipeline);
}

void colored_model_wireframe(Engine& engine)
{
  TwoStageShader shaders(engine, "colored_model_wireframe.vert", "colored_model_wireframe.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_LINE,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.colored_model_wireframe.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.colored_model_wireframe.pipeline);
}

void fft_water_hkt(Engine& engine)
{
  TwoStageShader shaders(engine, "fft_water_hkt.vert", "fft_water_hkt.frag");

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = 0,
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = sizeof(vec2),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = 4 * sizeof(float),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(FFT_WATER_H0_TEXTURE_DIM),
          .height   = static_cast<float>(FFT_WATER_H0_TEXTURE_DIM),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = {.width = FFT_WATER_H0_TEXTURE_DIM, .height = FFT_WATER_H0_TEXTURE_DIM},
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shaders.shader_stages),
      .pStages             = shaders.shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.pipelines.fft_water_hkt.layout,
      .renderPass          = engine.render_passes.water_pre_pass.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &engine.pipelines.fft_water_hkt.pipeline);
}

} // namespace

void tesselated_ground(Engine& engine, float y_scale = 2.0f, float y_offset = -12.0f)
{
  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("tesselated_ground.vert"),
          .pName  = "main",
      },
      {

          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .module = engine.load_shader("tesselated_ground.tesc"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .module = engine.load_shader("tesselated_ground.tese"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("tesselated_ground.frag"),
          .pName  = "main",
      },
  };

  // control

  VkSpecializationMapEntry tesc_specialization_entries[] = {
      {
          .constantID = 0,
          .offset     = 0,
          .size       = sizeof(float),
      },
      {
          .constantID = 1,
          .offset     = sizeof(float),
          .size       = sizeof(float),
      },
      {
          .constantID = 2,
          .offset     = 2 * sizeof(float),
          .size       = sizeof(float),
      },
      {
          .constantID = 3,
          .offset     = 3 * sizeof(float),
          .size       = sizeof(float),
      },
      {
          .constantID = 4,
          .offset     = 4 * sizeof(float),
          .size       = sizeof(float),
      },
  };

  const float tesc_constants[] = {
      5.0f,  // tessellatedEdgeSize
      0.01f, // tessellationFactor
      20.0f, // frustum_check_radius
      y_scale, y_offset,
  };

  VkSpecializationInfo tesc_specialization_info = {
      .mapEntryCount = SDL_arraysize(tesc_specialization_entries),
      .pMapEntries   = tesc_specialization_entries,
      .dataSize      = sizeof(tesc_constants),
      .pData         = tesc_constants,
  };

  shader_stages[1].pSpecializationInfo = &tesc_specialization_info;

  // evaluation
  VkSpecializationMapEntry tese_specialization_entries[] = {
      {
          .constantID = 0,
          .offset     = 0,
          .size       = sizeof(float),
      },
      {
          .constantID = 1,
          .offset     = sizeof(float),
          .size       = sizeof(float),
      },
  };

  const float tese_constants[] = {
      y_scale,
      y_offset,
  };

  VkSpecializationInfo tese_specialization_info = {
      .mapEntryCount = SDL_arraysize(tese_specialization_entries),
      .pMapEntries   = tese_specialization_entries,
      .dataSize      = sizeof(tese_constants),
      .pData         = tese_constants,
  };

  shader_stages[2].pSpecializationInfo = &tese_specialization_info;

  VkVertexInputAttributeDescription attribute_descriptions[] = {
      {
          .location = 0,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position)),
      },
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32B32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal)),
      },
      {
          .location = 2,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord)),
      },
  };

  VkVertexInputBindingDescription vertex_binding_descriptions[] = {
      {
          .binding   = 0,
          .stride    = sizeof(TrianglesVertex),
          .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions),
      .pVertexBindingDescriptions      = vertex_binding_descriptions,
      .vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions),
      .pVertexAttributeDescriptions    = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology               = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewports[] = {
      {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(engine.extent2D.width),
          .height   = static_cast<float>(engine.extent2D.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent = engine.extent2D,
      },
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = SDL_arraysize(viewports),
      .pViewports    = viewports,
      .scissorCount  = SDL_arraysize(scissors),
      .pScissors     = scissors,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_FRONT_BIT,
      .frontFace               = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples  = engine.MSAA_SAMPLE_COUNT,
      .sampleShadingEnable   = VK_TRUE,
      .minSampleShading      = 1.0f,
      .alphaToCoverageEnable = VK_TRUE,
      .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
  };

  VkColorComponentFlags rgba_mask = 0;
  rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
  rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
      {
          .blendEnable         = VK_FALSE,
          .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
          .colorBlendOp        = VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp        = VK_BLEND_OP_ADD,
          .colorWriteMask      = rgba_mask,
      },
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = SDL_arraysize(color_blend_attachments),
      .pAttachments    = color_blend_attachments,
  };

  VkPipelineTessellationStateCreateInfo tessellation_state = {
      .sType              = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .patchControlPoints = 4,
  };

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_LINE_WIDTH};

  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = SDL_arraysize(dynamic_states),
      .pDynamicStates    = dynamic_states,
  };

  VkGraphicsPipelineCreateInfo ci = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pTessellationState  = &tessellation_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state,
      .layout              = engine.pipelines.tesselated_ground.layout,
      .renderPass          = engine.render_passes.color_and_depth.render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.pipelines.tesselated_ground.pipeline);

  for (VkPipelineShaderStageCreateInfo& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void Engine::setup_pipelines()
{
  shadow_mapping(*this);
  skybox(*this);
  scene3d(*this);
  colored_geometry(*this);
  colored_geometry_triangle_strip(*this);
  colored_geometry_skinned(*this);
  imgui(*this);
  green_gui(*this);
  green_gui_weapon_selector_box_left(*this);
  green_gui_weapon_selector_box_right(*this);
  green_gui_lines(*this);
  green_gui_sdf(*this);
  green_gui_triangle(*this);
  green_gui_radar_dots(*this);
  pbr_water(*this);
  debug_billboard(*this);
  debug_billboard_texture_array(*this);
  colored_model_wireframe(*this);
  tesselated_ground(*this);
  fft_water_hkt(*this);
}
