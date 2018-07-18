#include "cubemap.hh"
#include "engine.hh"
#include "game.hh"
#include <SDL2/SDL_log.h>
#include <linmath.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (static_cast<float>(M_PI) * deg) / 180.0f;
}

constexpr float calculate_mip_divisor(int mip_level)
{
  return static_cast<float>(mip_level ? SDL_pow(2, mip_level) : 1);
}

void generate_cubemap_views(mat4x4 views[6])
{
  struct V3
  {
    vec3 data;
  };

  auto look_at = [](mat4x4 result, V3 center, V3 up) {
    vec3 eye = {0.0f, 0.0f, 0.0f};
    mat4x4_look_at(result, eye, center.data, up.data);
  };

  look_at(views[0], {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
  look_at(views[1], {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
  look_at(views[2], {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
  look_at(views[3], {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  look_at(views[4], {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f});
  look_at(views[5], {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f});
}

} // namespace

int generate_cubemap(Engine* engine, Game* game, const char* equirectangular_filepath, int desired_size[2])
{
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  //////////////////////////////////////////////////////////////////////////////
  // Result cubemap image handle creation
  //////////////////////////////////////////////////////////////////////////////
  VkImage cubemap_image = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags     = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = surface_format,
        .extent =
            {
                .width  = static_cast<uint32_t>(desired_size[0]),
                .height = static_cast<uint32_t>(desired_size[1]),
                .depth  = 1,
            },
        .mipLevels     = 1,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(egh.device, &ci, nullptr, &cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, cubemap_image, &reqs);
    vkBindImageMemory(egh.device, cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Image view containing all 6 cubemap layers
  //////////////////////////////////////////////////////////////////////////////
  VkImageView cubemap_image_view = VK_NULL_HANDLE;

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 6,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cubemap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = surface_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_view);
  }

  //////////////////////////////////////////////////////////////////////////////
  // 6 Image views for each of the cubemaps layer
  //////////////////////////////////////////////////////////////////////////////
  VkImageView cubemap_image_side_views[6];

  for (unsigned i = 0; i < SDL_arraysize(cubemap_image_side_views); ++i)
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = static_cast<uint32_t>(i),
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cubemap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = surface_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_side_views[i]);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Push image and image view handles to engine list
  //////////////////////////////////////////////////////////////////////////////
  int result_idx = engine->images.loaded_count;
  engine->images.add(cubemap_image, cubemap_image_view);

  //////////////////////////////////////////////////////////////////////////////
  // Load 2D equirectangular image from file
  //////////////////////////////////////////////////////////////////////////////
  int plain_texture_idx = engine->load_texture(equirectangular_filepath);

  //////////////////////////////////////////////////////////////////////////////
  // cubemap creation plan:
  //
  // - equirectangular image will be mapped as a skybox.
  // - 6 camera angles will be used to render all possible view directions.
  // - each render result will be stored in separate cubemap layer
  // - each cubemap layer will be a separate render pass attachment
  // - 6 passes will iterate through all attachments and camera views
  //////////////////////////////////////////////////////////////////////////////
  VkRenderPass render_pass = VK_NULL_HANDLE;

  {
    VkAttachmentDescription attachments[6] = {};

    for (VkAttachmentDescription& a : attachments)
    {
      a.format         = surface_format;
      a.samples        = VK_SAMPLE_COUNT_1_BIT;
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      a.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[6] = {};
    for (unsigned i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[6] = {};
    for (unsigned i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = SDL_arraysize(attachments),
        .pAttachments    = attachments,
        .subpassCount    = SDL_arraysize(subpasses),
        .pSubpasses      = subpasses,
    };

    vkCreateRenderPass(egh.device, &ci, nullptr, &render_pass);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Only single descriptor set will be used - for equirectangular image sampler
  //////////////////////////////////////////////////////////////////////////////
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

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

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &descriptor_set_layout);
  }

  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

  {
    VkDescriptorSetAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = egh.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &descriptor_set_layout,
    };

    vkAllocateDescriptorSets(egh.device, &info, &descriptor_set);
  }

  {
    VkDescriptorImageInfo image = {
        .sampler     = egh.texture_sampler,
        .imageView   = engine->images.image_views[plain_texture_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptor_set,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image,
    };

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  //////////////////////////////////////////////////////////////////////////////
  // mvp matrix will be pushed at vertex shader stage
  //////////////////////////////////////////////////////////////////////////////
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

  {
    VkPushConstantRange range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = 16 * sizeof(float),
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &range,
    };

    vkCreatePipelineLayout(egh.device, &ci, nullptr, &pipeline_layout);
  }

  VkPipeline pipelines[6];

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = engine->load_shader("equirectangular_to_cubemap.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = engine->load_shader("equirectangular_to_cubemap.frag.spv"),
            .pName  = "main",
        },
    };

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description = {
        .location = 0,
        .binding  = 0,
        .format   = VK_FORMAT_R32G32B32_SFLOAT,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, position)),
    };

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions    = &attribute_description,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(desired_size[0]),
        .height   = static_cast<float>(desired_size[1]),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {.width = static_cast<uint32_t>(desired_size[0]), .height = static_cast<uint32_t>(desired_size[1])},
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
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
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_FALSE,
        .depthWriteEnable      = VK_FALSE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f,
    };

    const VkColorComponentFlags rgba_components =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = rgba_components,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment,
    };

    for (unsigned i = 0; i < SDL_arraysize(pipelines); ++i)
    {
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
          .subpass             = static_cast<uint32_t>(i),
          .basePipelineHandle  = VK_NULL_HANDLE,
          .basePipelineIndex   = -1,
      };

      vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipelines[i]);
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  VkFramebuffer framebuffer = VK_NULL_HANDLE;

  {
    VkFramebufferCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = render_pass,
        .attachmentCount = SDL_arraysize(cubemap_image_side_views),
        .pAttachments    = cubemap_image_side_views,
        .width           = static_cast<uint32_t>(desired_size[0]),
        .height          = static_cast<uint32_t>(desired_size[1]),
        .layers          = 1,
    };

    vkCreateFramebuffer(egh.device, &ci, nullptr, &framebuffer);
  }

  //////////////////////////////////////////////////////////////////////////////
  // render
  //////////////////////////////////////////////////////////////////////////////

  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = egh.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_values[6] = {};
      for (VkClearValue& clear_value : clear_values)
        clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

      VkExtent2D extent = {
          .width  = static_cast<uint32_t>(desired_size[0]),
          .height = static_cast<uint32_t>(desired_size[1]),
      };

      VkRenderPassBeginInfo begin = {
          .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass      = render_pass,
          .framebuffer     = framebuffer,
          .renderArea      = {.extent = extent},
          .clearValueCount = SDL_arraysize(clear_values),
          .pClearValues    = clear_values,
      };

      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    mat4x4 projection{};
    mat4x4_perspective(projection, to_rad(90.0f), 1.0f, 0.1f, 100.0f);

    mat4x4 views[6]{};
    generate_cubemap_views(views);

    for (int i = 0; i < 6; ++i)
    {
      mat4x4 projectionview{};
      mat4x4_mul(projectionview, projection, views[i]);

      mat4x4 model{};
      mat4x4_identity(model);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projectionview, model);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
      vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);

      game->box.renderRaw(*engine, cmd);
      if (5 != i)
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // @todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  vkDestroyFramebuffer(egh.device, framebuffer, nullptr);

  for (VkImageView& image_view : cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, render_pass, nullptr);

  // original equirectangular image is no longer needed
  vkDestroyImage(egh.device, engine->images.images[plain_texture_idx], nullptr);
  vkDestroyImageView(egh.device, engine->images.image_views[plain_texture_idx], nullptr);
  engine->images.pop();
  engine->images.loaded_count -= 1;

  return result_idx;
}

int generate_irradiance_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2])
{
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  //////////////////////////////////////////////////////////////////////////////
  // Result cubemap image handle creation
  //////////////////////////////////////////////////////////////////////////////
  VkImage cubemap_image = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags     = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = surface_format,
        .extent =
            {
                .width  = static_cast<uint32_t>(desired_size[0]),
                .height = static_cast<uint32_t>(desired_size[1]),
                .depth  = 1,
            },
        .mipLevels     = 1,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(egh.device, &ci, nullptr, &cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, cubemap_image, &reqs);
    vkBindImageMemory(egh.device, cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Image views creation
  //////////////////////////////////////////////////////////////////////////////
  VkImageView cubemap_image_view = VK_NULL_HANDLE;

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 6,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cubemap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = surface_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_view);
  }

  VkImageView cubemap_image_side_views[6];

  for (unsigned i = 0; i < SDL_arraysize(cubemap_image_side_views); ++i)
  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = static_cast<uint32_t>(i),
        .layerCount     = 1,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cubemap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = surface_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_side_views[i]);
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(cubemap_image, cubemap_image_view);

  VkRenderPass render_pass = VK_NULL_HANDLE;

  {
    VkAttachmentDescription attachments[6] = {};
    for (VkAttachmentDescription& a : attachments)
    {
      a.format         = surface_format;
      a.samples        = VK_SAMPLE_COUNT_1_BIT;
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      a.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[6] = {};
    for (unsigned i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[6]{};
    for (unsigned i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = SDL_arraysize(attachments),
        .pAttachments    = attachments,
        .subpassCount    = SDL_arraysize(subpasses),
        .pSubpasses      = subpasses,
    };

    vkCreateRenderPass(egh.device, &ci, nullptr, &render_pass);
  }

  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

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

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &descriptor_set_layout);
  }

  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

  {
    VkDescriptorSetAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = egh.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &descriptor_set_layout,
    };

    vkAllocateDescriptorSets(egh.device, &info, &descriptor_set);
  }

  {
    VkDescriptorImageInfo image = {
        .sampler     = egh.texture_sampler,
        .imageView   = engine->images.image_views[environment_cubemap_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptor_set,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image,
    };

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

  {
    VkPushConstantRange range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = 16 * sizeof(float),
    };

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &range,
    };

    vkCreatePipelineLayout(egh.device, &ci, nullptr, &pipeline_layout);
  }

  VkPipeline pipelines[6];

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = engine->load_shader("cubemap_to_irradiance.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = engine->load_shader("cubemap_to_irradiance.frag.spv"),
            .pName  = "main",
        },
    };

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description = {
        .location = 0,
        .binding  = 0,
        .format   = VK_FORMAT_R32G32B32_SFLOAT,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, position)),
    };

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions    = &attribute_description,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(desired_size[0]),
        .height   = static_cast<float>(desired_size[1]),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {.width = static_cast<uint32_t>(desired_size[0]), .height = static_cast<uint32_t>(desired_size[1])},
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
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
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_FALSE,
        .depthWriteEnable      = VK_FALSE,
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

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = rgba_mask,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment,
    };

    for (unsigned i = 0; i < SDL_arraysize(pipelines); ++i)
    {
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
          .subpass             = static_cast<uint32_t>(i),
          .basePipelineHandle  = VK_NULL_HANDLE,
          .basePipelineIndex   = -1,
      };

      vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipelines[i]);
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  VkFramebuffer framebuffer = VK_NULL_HANDLE;

  {
    VkFramebufferCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = render_pass,
        .attachmentCount = SDL_arraysize(cubemap_image_side_views),
        .pAttachments    = cubemap_image_side_views,
        .width           = static_cast<uint32_t>(desired_size[0]),
        .height          = static_cast<uint32_t>(desired_size[1]),
        .layers          = 1,
    };

    vkCreateFramebuffer(egh.device, &ci, nullptr, &framebuffer);
  }

  //////////////////////////////////////////////////////////////////////////////
  // render
  //////////////////////////////////////////////////////////////////////////////
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = egh.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_values[6] = {};
      for (VkClearValue& clear_value : clear_values)
        clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

      VkExtent2D extent = {
          .width  = static_cast<uint32_t>(desired_size[0]),
          .height = static_cast<uint32_t>(desired_size[1]),
      };

      VkRenderPassBeginInfo begin = {
          .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass      = render_pass,
          .framebuffer     = framebuffer,
          .renderArea      = {.extent = extent},
          .clearValueCount = SDL_arraysize(clear_values),
          .pClearValues    = clear_values,
      };

      vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    }

    mat4x4 projection{};
    mat4x4_perspective(projection, to_rad(90.0f), 1.0f, 0.1f, 100.0f);

    mat4x4 views[6]{};
    generate_cubemap_views(views);

    for (int i = 0; i < 6; ++i)
    {
      mat4x4 projectionview{};
      mat4x4_mul(projectionview, projection, views[i]);

      mat4x4 model{};
      mat4x4_identity(model);

      mat4x4 mvp = {};
      mat4x4_mul(mvp, projectionview, model);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
      vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      game->box.renderRaw(*engine, cmd);

      if (5 != i)
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  vkDestroyFramebuffer(egh.device, framebuffer, nullptr);

  for (VkImageView& image_view : cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, render_pass, nullptr);

  return result_idx;
}

int generate_prefiltered_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2])
{
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  constexpr int CUBE_SIDES         = 6;
  constexpr int DESIRED_MIP_LEVELS = 5;

  //////////////////////////////////////////////////////////////////////////////
  // Result cubemap image handle creation
  //////////////////////////////////////////////////////////////////////////////
  VkImage cubemap_image = VK_NULL_HANDLE;

  {
    VkImageCreateInfo ci = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags     = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = surface_format,
        .extent =
            {
                .width  = static_cast<uint32_t>(desired_size[0]),
                .height = static_cast<uint32_t>(desired_size[1]),
                .depth  = 1,
            },
        .mipLevels     = DESIRED_MIP_LEVELS,
        .arrayLayers   = CUBE_SIDES,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    vkCreateImage(egh.device, &ci, nullptr, &cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, cubemap_image, &reqs);
    vkBindImageMemory(egh.device, cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Image view creation
  //////////////////////////////////////////////////////////////////////////////
  VkImageView cubemap_image_view = VK_NULL_HANDLE;

  {
    VkImageSubresourceRange sr = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = DESIRED_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount     = CUBE_SIDES,
    };

    VkImageViewCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = cubemap_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        .format           = surface_format,
        .subresourceRange = sr,
    };

    vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_view);
  }

  VkImageView cubemap_image_side_views[CUBE_SIDES * DESIRED_MIP_LEVELS];

  for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
  {
    for (int cube_side = 0; cube_side < CUBE_SIDES; ++cube_side)
    {
      VkImageSubresourceRange sr = {
          .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = static_cast<uint32_t>(mip_level),
          .levelCount     = 1,
          .baseArrayLayer = static_cast<uint32_t>(cube_side),
          .layerCount     = 1,
      };

      VkImageViewCreateInfo ci = {
          .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image            = cubemap_image,
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = surface_format,
          .subresourceRange = sr,
      };

      vkCreateImageView(egh.device, &ci, nullptr, &cubemap_image_side_views[CUBE_SIDES * mip_level + cube_side]);
    }
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(cubemap_image, cubemap_image_view);

  VkRenderPass render_pass = VK_NULL_HANDLE;

  {
    VkAttachmentDescription attachments[CUBE_SIDES] = {};
    for (VkAttachmentDescription& a : attachments)
    {
      a.format         = surface_format;
      a.samples        = VK_SAMPLE_COUNT_1_BIT;
      a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      a.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      a.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[CUBE_SIDES] = {};
    for (unsigned i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[CUBE_SIDES] = {};
    for (unsigned i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = SDL_arraysize(attachments),
        .pAttachments    = attachments,
        .subpassCount    = SDL_arraysize(subpasses),
        .pSubpasses      = subpasses,
    };

    vkCreateRenderPass(egh.device, &ci, nullptr, &render_pass);
  }

  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

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

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &descriptor_set_layout);
  }

  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

  {
    VkDescriptorSetAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = egh.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &descriptor_set_layout,
    };

    vkAllocateDescriptorSets(egh.device, &info, &descriptor_set);
  }

  {
    VkDescriptorImageInfo image = {
        .sampler     = egh.texture_sampler,
        .imageView   = engine->images.image_views[environment_cubemap_idx],
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptor_set,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image,
    };

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

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
        .pSetLayouts            = &descriptor_set_layout,
        .pushConstantRangeCount = 2,
        .pPushConstantRanges    = ranges,
    };

    vkCreatePipelineLayout(egh.device, &ci, nullptr, &pipeline_layout);
  }

  VkPipeline pipelines[CUBE_SIDES * DESIRED_MIP_LEVELS];

  {
    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = engine->load_shader("cubemap_prefiltering.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = engine->load_shader("cubemap_prefiltering.frag.spv"),
            .pName  = "main",
        },
    };

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description = {
        .location = 0,
        .binding  = 0,
        .format   = VK_FORMAT_R32G32B32_SFLOAT,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, position)),
    };

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions    = &attribute_description,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
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
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_FALSE,
        .depthWriteEnable      = VK_FALSE,
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

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = rgba_mask,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment,
    };

    for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
    {
      VkViewport viewport = {
          .x        = 0.0f,
          .y        = 0.0f,
          .width    = static_cast<float>(desired_size[0]) / calculate_mip_divisor(mip_level),
          .height   = static_cast<float>(desired_size[1]) / calculate_mip_divisor(mip_level),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      };

      VkRect2D scissor = {
          .offset = {0, 0},
          .extent =
              {
                  .width  = static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level),
                  .height = static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level),
              },
      };

      VkPipelineViewportStateCreateInfo viewport_state = {
          .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
          .viewportCount = 1,
          .pViewports    = &viewport,
          .scissorCount  = 1,
          .pScissors     = &scissor,
      };

      for (int cube_side = 0; cube_side < CUBE_SIDES; ++cube_side)
      {
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
            .subpass             = static_cast<uint32_t>(cube_side),
            .basePipelineHandle  = VK_NULL_HANDLE,
            .basePipelineIndex   = -1,
        };

        vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                  &pipelines[CUBE_SIDES * mip_level + cube_side]);
      }
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  VkFramebuffer framebuffers[DESIRED_MIP_LEVELS];

  for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
  {
    VkFramebufferCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = render_pass,
        .attachmentCount = CUBE_SIDES,
        .pAttachments    = &cubemap_image_side_views[CUBE_SIDES * mip_level],
        .width           = static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level),
        .height          = static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level),
        .layers          = 1,
    };

    vkCreateFramebuffer(egh.device, &ci, nullptr, &framebuffers[mip_level]);
  }

  // -------------------------------------------------------------------------------------
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = egh.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
    {
      {
        VkClearValue clear_values[CUBE_SIDES] = {};
        for (VkClearValue& clear_value : clear_values)
          clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        VkExtent2D extent = {
            .width  = static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level),
            .height = static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level),
        };

        VkRenderPassBeginInfo begin = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass      = render_pass,
            .framebuffer     = framebuffers[mip_level],
            .renderArea      = {.extent = extent},
            .clearValueCount = SDL_arraysize(clear_values),
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
      }

      mat4x4 projection{};
      mat4x4_perspective(projection, to_rad(90.0f), 1.0f, 0.1f, 100.0f);

      mat4x4 views[6]{};
      generate_cubemap_views(views);

      const float roughness = (float)mip_level / (float)(DESIRED_MIP_LEVELS - 1);
      for (int cube_side = 0; cube_side < CUBE_SIDES; ++cube_side)
      {
        mat4x4 projectionview{};
        mat4x4_mul(projectionview, projection, views[cube_side]);

        mat4x4 model{};
        mat4x4_identity(model);

        mat4x4 mvp = {};
        mat4x4_mul(mvp, projectionview, model);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[CUBE_SIDES * mip_level + cube_side]);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0,
                                nullptr);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(float),
                           &roughness);
        game->box.renderRaw(*engine, cmd);

        if (5 != cube_side)
          vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
      }

      vkCmdEndRenderPass(cmd);
    }
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  for (VkFramebuffer& framebuffer : framebuffers)
    vkDestroyFramebuffer(egh.device, framebuffer, nullptr);

  for (VkImageView& image_view : cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, render_pass, nullptr);

  return result_idx;
}

int generate_brdf_lookup(Engine* engine, int size)
{
  VkImage brdf_image = VK_NULL_HANDLE;

  {
    VkImageCreateInfo info = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format    = VK_FORMAT_R16G16_SFLOAT,
        .extent =
            {
                .width  = static_cast<uint32_t>(size),
                .height = static_cast<uint32_t>(size),
                .depth  = 1,
            },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    vkCreateImage(engine->generic_handles.device, &info, nullptr, &brdf_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(engine->generic_handles.device, brdf_image, &reqs);
    vkBindImageMemory(engine->generic_handles.device, brdf_image, engine->images.memory,
                      engine->images.allocate(reqs.size));
  }

  VkImageView brdf_image_view = VK_NULL_HANDLE;

  {
    VkImageSubresourceRange range = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    VkImageViewCreateInfo info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = brdf_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_R16G16_SFLOAT,
        .subresourceRange = range,
    };

    vkCreateImageView(engine->generic_handles.device, &info, nullptr, &brdf_image_view);
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(brdf_image, brdf_image_view);

  VkRenderPass render_pass = VK_NULL_HANDLE;

  {
    VkAttachmentDescription attachment = {
        .format         = VK_FORMAT_R16G16_SFLOAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkAttachmentReference reference = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass_description = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &reference,
    };

    VkSubpassDependency dependencies[] = {
        {
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = 0,
            .srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask   = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        {
            .srcSubpass      = 0,
            .dstSubpass      = VK_SUBPASS_EXTERNAL,
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
    };

    VkRenderPassCreateInfo create = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &attachment,
        .subpassCount    = 1,
        .pSubpasses      = &subpass_description,
        .dependencyCount = 2,
        .pDependencies   = dependencies,
    };

    vkCreateRenderPass(engine->generic_handles.device, &create, nullptr, &render_pass);
  }

  VkFramebuffer framebuffer = VK_NULL_HANDLE;

  {
    VkFramebufferCreateInfo create = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = render_pass,
        .attachmentCount = 1,
        .pAttachments    = &brdf_image_view,
        .width           = static_cast<uint32_t>(size),
        .height          = static_cast<uint32_t>(size),
        .layers          = 1,
    };

    vkCreateFramebuffer(engine->generic_handles.device, &create, nullptr, &framebuffer);
  }

  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

  {
    VkPipelineLayoutCreateInfo create = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(engine->generic_handles.device, &create, nullptr, &pipeline_layout);
  }

  VkPipeline pipeline = VK_NULL_HANDLE;

  {
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_NONE,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.0f,
    };

    VkColorComponentFlags rgba_mask = 0;
    rgba_mask |= VK_COLOR_COMPONENT_R_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_G_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_B_BIT;
    rgba_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable    = VK_FALSE,
        .colorWriteMask = rgba_mask,
    };

    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL,
        .front            = depth_stencil.back,
        .back             = {.compareOp = VK_COMPARE_OP_ALWAYS},
    };

    VkPipelineViewportStateCreateInfo viewport = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicStateCI = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = SDL_arraysize(dynamic_states),
        .pDynamicStates    = dynamic_states,
    };

    VkPipelineVertexInputStateCreateInfo emptyInputStateCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = engine->load_shader("brdf_compute.vert.spv"),
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = engine->load_shader("brdf_compute.frag.spv"),
            .pName  = "main",
        },
    };

    VkGraphicsPipelineCreateInfo create = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,
        .pStages             = shader_stages,
        .pVertexInputState   = &emptyInputStateCI,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &color_blend,
        .pDynamicState       = &dynamicStateCI,
        .layout              = pipeline_layout,
        .renderPass          = render_pass,
    };

    vkCreateGraphicsPipelines(engine->generic_handles.device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline);
    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(engine->generic_handles.device, shader_stage.module, nullptr);
  }

  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = engine->generic_handles.graphics_command_pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
      };

      vkAllocateCommandBuffers(engine->generic_handles.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_value{};
      clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

      VkExtent2D extent = {
          .width  = static_cast<uint32_t>(size),
          .height = static_cast<uint32_t>(size),
      };

      VkRenderPassBeginInfo info = {
          .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass      = render_pass,
          .framebuffer     = framebuffer,
          .renderArea      = {.extent = extent},
          .clearValueCount = 1,
          .pClearValues    = &clear_value,
      };

      vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    VkViewport viewport = {
        .width    = static_cast<float>(size),
        .height   = static_cast<float>(size),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .extent =
            {
                .width  = static_cast<uint32_t>(size),
                .height = static_cast<uint32_t>(size),
            },
    };

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vkCreateFence(engine->generic_handles.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit = {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &cmd,
      };

      vkQueueSubmit(engine->generic_handles.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(engine->generic_handles.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine->generic_handles.device, image_generation_fence, nullptr);
  }

  vkDestroyPipeline(engine->generic_handles.device, pipeline, nullptr);
  vkDestroyPipelineLayout(engine->generic_handles.device, pipeline_layout, nullptr);
  vkDestroyFramebuffer(engine->generic_handles.device, framebuffer, nullptr);
  vkDestroyRenderPass(engine->generic_handles.device, render_pass, nullptr);

  return result_idx;
}
