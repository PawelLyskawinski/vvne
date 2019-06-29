#include "fft_water.hh"

namespace {

VkExtent3D h0_texture_dimension = {
    .width  = 512,
    .height = 512,
    .depth  = 1,
};

Texture create_h0_k_texture(Engine& engine)
{
  Texture result = {};

  {
    VkImageCreateInfo info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = engine.surface_format.format,
        .extent        = h0_texture_dimension,
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(engine.device, &info, nullptr, &result.image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(engine.device, result.image, &reqs);
    result.memory_offset =
        engine.memory_blocks.device_images.allocator.allocate_bytes(align(reqs.size, reqs.alignment));
    vkBindImageMemory(engine.device, result.image, engine.memory_blocks.device_images.memory, result.memory_offset);
  }

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = result.image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = engine.surface_format.format,
        .subresourceRange = sr,
    };

    vkCreateImageView(engine.device, &ci, nullptr, &result.image_view);
  }

  return result;
}

VkRenderPass create_h0_k_render_pass(Engine& engine)
{
  VkRenderPass render_pass = VK_NULL_HANDLE;

  VkAttachmentDescription attachments[] = {
      {
          .format         = engine.surface_format.format,
          .samples        = VK_SAMPLE_COUNT_1_BIT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkAttachmentReference references[] = {
      {
          .attachment = 0,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkSubpassDescription subpass = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments    = references,
  };

  VkSubpassDependency dependencies[] = {
      {
          .srcSubpass    = VK_SUBPASS_EXTERNAL,
          .dstSubpass    = 0,
          .srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      },
      {
          .srcSubpass    = 0,
          .dstSubpass    = VK_SUBPASS_EXTERNAL,
          .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .dstAccessMask = 0,
      },
  };

  VkRenderPassCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = SDL_arraysize(attachments),
      .pAttachments    = attachments,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = SDL_arraysize(dependencies),
      .pDependencies   = dependencies,
  };

  vkCreateRenderPass(engine.device, &ci, nullptr, &render_pass);

  return render_pass;
}

VkPipelineLayout create_h0_k_pipeline_layout(Engine& engine)
{
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

  VkPipelineLayoutCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };

  vkCreatePipelineLayout(engine.device, &ci, nullptr, &pipeline_layout);

  return pipeline_layout;
}

VkPipeline create_h0_k_pipeline(Engine& engine, VkRenderPass render_pass, VkPipelineLayout pipeline_layout)
{
  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_VERTEX_BIT,
          .module = engine.load_shader("fft_water_h0_k_pass.vert"),
          .pName  = "main",
      },
      {

          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = engine.load_shader("fft_water_h0_k_pass.frag"),
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
      {
          .location = 1,
          .binding  = 0,
          .format   = VK_FORMAT_R32G32_SFLOAT,
          .offset   = 2 * sizeof(float),
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
          .width    = static_cast<float>(h0_texture_dimension.width),
          .height   = static_cast<float>(h0_texture_dimension.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      },
  };

  VkRect2D scissors[] = {
      {
          .offset = {0, 0},
          .extent =
              {
                  .width  = h0_texture_dimension.width,
                  .height = h0_texture_dimension.height,
              },
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
      .stageCount          = SDL_arraysize(shader_stages),
      .pStages             = shader_stages,
      .pVertexInputState   = &vertex_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization_state,
      .pMultisampleState   = &multisample_state,
      .pDepthStencilState  = &depth_stencil_state,
      .pColorBlendState    = &color_blend_state,
      .layout              = pipeline_layout,
      .renderPass          = render_pass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1,
  };

  VkPipeline pipeline = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(engine.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);

  for (VkPipelineShaderStageCreateInfo& shader_stage : shader_stages)
  {
    vkDestroyShaderModule(engine.device, shader_stage.module, nullptr);
  }

  return pipeline;
}

VkFramebuffer create_h0_k_framebuffer(Engine& engine, VkRenderPass render_pass, VkImageView target_view)
{
  VkFramebufferCreateInfo info = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = render_pass,
      .attachmentCount = 1,
      .pAttachments    = &target_view,
      .width           = h0_texture_dimension.width,
      .height          = h0_texture_dimension.height,
      .layers          = 1,
  };

  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  vkCreateFramebuffer(engine.device, &info, nullptr, &framebuffer);
  return framebuffer;
}

} // namespace

namespace fft_water {

void generate_h0_k_image(Engine& engine, VkDeviceSize offset_to_billboard_vertices, Texture& fft_water_h0_k_texture,
                         Texture& fft_water_h0_minus_k_texture)
{
  fft_water_h0_k_texture       = create_h0_k_texture(engine);
  fft_water_h0_minus_k_texture = create_h0_k_texture(engine);

  VkRenderPass     render_pass     = create_h0_k_render_pass(engine);
  VkPipelineLayout pipeline_layout = create_h0_k_pipeline_layout(engine);
  VkPipeline       pipeline        = create_h0_k_pipeline(engine, render_pass, pipeline_layout);
  VkFramebuffer    framebuffer     = create_h0_k_framebuffer(engine, render_pass, fft_water_h0_k_texture.image_view);

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = engine.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    vkAllocateCommandBuffers(engine.device, &allocate, &command_buffer);
  }

  {
    VkCommandBufferInheritanceInfo inheritance_info = {
        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .renderPass  = render_pass,
        .subpass     = 0,
        .framebuffer = framebuffer,
    };

    VkCommandBufferBeginInfo begin = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = &inheritance_info,
    };

    vkBeginCommandBuffer(command_buffer, &begin);
  }

  // entry barrier for writing
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = fft_water_h0_k_texture.image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }

  {
    VkClearValue clear_value = {};
    clear_value.color        = {{0.0, 0.0, 0.0, 1.0}};

    VkRenderPassBeginInfo info = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = render_pass,
        .framebuffer = framebuffer,
        .renderArea =
            {
                .offset = {0, 0},
                .extent = {.width = h0_texture_dimension.width, .height = h0_texture_dimension.height},
            },
        .clearValueCount = 1,
        .pClearValues    = &clear_value,
    };

    vkCmdBeginRenderPass(command_buffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &engine.gpu_device_local_memory_buffer, &offset_to_billboard_vertices);
    vkCmdDraw(command_buffer, 4, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
  }

  // after successful write, we'll freeze resources for read-only
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = fft_water_h0_k_texture.image,
        .subresourceRange    = sr,
    };

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }

  vkEndCommandBuffer(command_buffer);

  VkFence fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(engine.device, &ci, nullptr, &fence);
  }

  {
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &command_buffer,
    };
    vkQueueSubmit(engine.graphics_queue, 1, &submit, fence);
  }

  vkWaitForFences(engine.device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(engine.device, fence, nullptr);

  vkDestroyFramebuffer(engine.device, framebuffer, nullptr);
  vkDestroyPipeline(engine.device, pipeline, nullptr);
  vkDestroyPipelineLayout(engine.device, pipeline_layout, nullptr);
  vkDestroyRenderPass(engine.device, render_pass, nullptr);
}

} // namespace fft_water
