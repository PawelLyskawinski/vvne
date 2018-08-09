#include "pipelines.hh"
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

void schedule_destruction_if_needed(ScheduledPipelineDestruction* list, int* list_length, VkPipeline pipeline)
{
  if (VK_NULL_HANDLE != pipeline)
  {
    ScheduledPipelineDestruction destrution_promise = {
        .frame_countdown = SWAPCHAIN_IMAGES_COUNT,
        .pipeline        = pipeline,
    };

    list[*list_length] = destrution_promise;
    *list_length += 1;
  }
}

} // namespace

void pipeline_reload_simple_rendering_skybox_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Skybox]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("skybox.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("skybox.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Skybox],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::Skybox,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Skybox]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_scene3d_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("triangle_push.vert"),
          .pName  = "main",
      },
      {

          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("triangle_push.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::Scene3D],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::Objects3D,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::Scene3D]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_coloredgeometry_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("colored_geometry.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("colored_geometry.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout     = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometry],
      .renderPass = engine.simple_rendering.render_pass,
      .subpass    = Engine::SimpleRendering::Pass::Objects3D,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometry]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_coloredgeometry_triangle_strip_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometryTriangleStrip]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("colored_geometry.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("colored_geometry.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout =
          engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometryTriangleStrip],
      .renderPass         = engine.simple_rendering.render_pass,
      .subpass            = Engine::SimpleRendering::Pass::Objects3D,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(
      engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
      &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometryTriangleStrip]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_coloredgeometryskinned_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("colored_geometry_skinned.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("colored_geometry_skinned.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout     = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned],
      .renderPass = engine.simple_rendering.render_pass,
      .subpass    = Engine::SimpleRendering::Pass::Objects3D,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(
      engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
      &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ColoredGeometrySkinned]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_imgui_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ImGui]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("imgui.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("imgui.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::ImGui],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::ImGui,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::ImGui]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGui]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGui],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGui]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_weapon_selector_box_left_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_weapon_selector_box_left.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_weapon_selector_box_left.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout =
          engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft],
      .renderPass         = engine.simple_rendering.render_pass,
      .subpass            = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(
      engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
      &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxLeft]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_weapon_selector_box_right_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_weapon_selector_box_right.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_weapon_selector_box_right.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout =
          engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight],
      .renderPass         = engine.simple_rendering.render_pass,
      .subpass            = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(
      engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
      &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiWeaponSelectorBoxRight]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_lines_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiLines]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_lines.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_lines.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state_info,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiLines],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiLines]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_sdf_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_sdf.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_sdf.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state,
      .layout     = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont],
      .renderPass = engine.simple_rendering.render_pass,
      .subpass    = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiSdfFont]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_triangle_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiTriangle]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_triangle.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_triangle.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      //.pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState = &color_blend_state,
      //.pDynamicState       = &dynamic_state_info,
      .layout     = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiTriangle],
      .renderPass = engine.simple_rendering.render_pass,
      .subpass    = Engine::SimpleRendering::Pass::RobotGui,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiTriangle]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_green_gui_radar_dots_reload(Engine& engine)
{
  schedule_destruction_if_needed(
      engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
      engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("green_gui_radar_dots.vert"),
          .pName  = "main",
      },
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("green_gui_radar_dots.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pColorBlendState    = &color_blend_state,
      .pDynamicState       = &dynamic_state_info,
      .layout     = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots],
      .renderPass = engine.simple_rendering.render_pass,
      .subpass    = Engine::SimpleRendering::Pass::RadarDots,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::GreenGuiRadarDots]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}

void pipeline_reload_simple_rendering_pbr_water_reload(Engine& engine)
{
  schedule_destruction_if_needed(engine.scheduled_pipelines_destruction, &engine.scheduled_pipelines_destruction_count,
                                 engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::PbrWater]);

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("pbr_water.vert"),
          .pName  = "main",
      },
      {

          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("pbr_water.frag"),
          .pName  = "main",
      },
  };

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
      .rasterizationSamples  = MSAA_SAMPLE_COUNT,
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = engine.simple_rendering.pipeline_layouts[Engine::SimpleRendering::Pipeline::PbrWater],
      .renderPass          = engine.simple_rendering.render_pass,
      .subpass             = Engine::SimpleRendering::Pass::Objects3D,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                            &engine.simple_rendering.pipelines[Engine::SimpleRendering::Pipeline::PbrWater]);

  for (auto& shader_stage : shader_stages)
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
}
