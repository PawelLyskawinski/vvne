#include "cubemap.hh"
#include "engine.hh"
#include "game.hh"
#include <SDL2/SDL_log.h>
#include <linmath.h>

namespace {

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

constexpr float calculate_mip_divisor(int mip_level)
{
  return static_cast<float>(mip_level ? SDL_pow(2, mip_level) : 1);
}

void generate_cubemap_views(mat4x4 views[6])
{
  vec3 eye = {0.0f, 0.0f, 0.0f};

  struct Input
  {
    vec3 center;
    vec3 up;
  } inputs[] = {
      {
          {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
      },
      {
          {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
      },
      {
          {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
      },
      {
          {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
      },
      {
          {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f},
      },
      {
          {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f},
      },
  };

  for (int i = 0; i < SDL_arraysize(inputs); ++i)
    mat4x4_look_at(views[i], eye, inputs[i].center, inputs[i].up);
}

} // namespace

int generate_cubemap(Engine* engine, Game* game, const char* equirectangular_filepath, int desired_size[2])
{
  struct OperationContext
  {
    VkImage     cubemap_image;
    VkImageView cubemap_image_view;
    VkImageView cubemap_image_side_views[6];

    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipelines[6];
    VkFramebuffer         framebuffer;
    VkDescriptorSet       descriptor_set;
  };

  OperationContext        operation{};
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.format        = surface_format;
    ci.extent.width  = static_cast<uint32_t>(desired_size[0]);
    ci.extent.height = static_cast<uint32_t>(desired_size[1]);
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 6;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(egh.device, &ci, nullptr, &operation.cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, operation.cubemap_image, &reqs);
    vkBindImageMemory(egh.device, operation.cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = 0;
    sr.layerCount     = 6;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_CUBE;
    ci.subresourceRange = sr;
    ci.format           = surface_format;
    ci.image            = operation.cubemap_image;
    vkCreateImageView(egh.device, &ci, nullptr, &operation.cubemap_image_view);
  }

  for (int i = 0; i < SDL_arraysize(operation.cubemap_image_side_views); ++i)
  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = static_cast<uint32_t>(i);
    sr.layerCount     = 1;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ci.subresourceRange = sr;
    ci.format           = surface_format;
    ci.image            = operation.cubemap_image;
    vkCreateImageView(egh.device, &ci, nullptr, &operation.cubemap_image_side_views[i]);
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(operation.cubemap_image, operation.cubemap_image_view);
  int plain_texture_idx = engine->load_texture(equirectangular_filepath);

  {
    VkAttachmentDescription attachments[6]{};
    for (VkAttachmentDescription& attachment : attachments)
    {
      attachment.format         = surface_format;
      attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
      attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[6]{};
    for (int i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[6]{};
    for (int i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkSubpassDependency dependencies[6]{};
    for (VkSubpassDependency& dependency : dependencies)
    {
      dependency.srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    for (int i = 1; i < 6; ++i)
    {
      dependencies[i].srcSubpass = static_cast<uint32_t>(i - 1);
      dependencies[i].dstSubpass = static_cast<uint32_t>(i);
    }

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    ci.subpassCount    = SDL_arraysize(subpasses);
    ci.pSubpasses      = subpasses;
    ci.dependencyCount = SDL_arraysize(dependencies);
    ci.pDependencies   = dependencies;

    vkCreateRenderPass(egh.device, &ci, nullptr, &operation.render_pass);
  }

  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &binding;

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &operation.descriptor_set_layout);
  }

  {
    VkDescriptorSetAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool     = egh.descriptor_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &operation.descriptor_set_layout;
    vkAllocateDescriptorSets(egh.device, &info, &operation.descriptor_set);
  }

  {
    VkDescriptorImageInfo image{};
    image.sampler     = egh.texture_sampler;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine->images.image_views[plain_texture_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = operation.descriptor_set;

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  {
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset     = 0;
    range.size       = 16 * sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &operation.descriptor_set_layout;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &range;
    vkCreatePipelineLayout(egh.device, &ci, nullptr, &operation.pipeline_layout);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = engine->load_shader("equirectangular_to_cubemap.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = engine->load_shader("equirectangular_to_cubemap.frag.spv");
    shader_stages[1].pName  = "main";

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description{};
    attribute_description.location = 0;
    attribute_description.binding  = 0;
    attribute_description.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_description.offset   = static_cast<uint32_t>(offsetof(Vertex, position));

    VkVertexInputBindingDescription vertex_binding_description{};
    vertex_binding_description.binding   = 0;
    vertex_binding_description.stride    = sizeof(Vertex);
    vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = 1;
    vertex_input_state.pVertexBindingDescriptions      = &vertex_binding_description;
    vertex_input_state.vertexAttributeDescriptionCount = 1;
    vertex_input_state.pVertexAttributeDescriptions    = &attribute_description;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(desired_size[0]);
    viewport.height   = static_cast<float>(desired_size[1]);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset        = {0, 0};
    scissor.extent.width  = static_cast<uint32_t>(desired_size[0]);
    scissor.extent.height = static_cast<uint32_t>(desired_size[1]);

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = &viewport;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable       = VK_FALSE;
    depth_stencil_state.depthWriteEnable      = VK_FALSE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.blendEnable         = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments    = &color_blend_attachment;

    for (int i = 0; i < SDL_arraysize(operation.pipelines); ++i)
    {
      VkGraphicsPipelineCreateInfo ci{};
      ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      ci.stageCount          = SDL_arraysize(shader_stages);
      ci.pStages             = shader_stages;
      ci.pVertexInputState   = &vertex_input_state;
      ci.pInputAssemblyState = &input_assembly_state;
      ci.pViewportState      = &viewport_state;
      ci.pRasterizationState = &rasterization_state;
      ci.pMultisampleState   = &multisample_state;
      ci.pDepthStencilState  = &depth_stencil_state;
      ci.pColorBlendState    = &color_blend_state;
      ci.layout              = operation.pipeline_layout;
      ci.renderPass          = operation.render_pass;
      ci.subpass             = static_cast<uint32_t>(i);
      ci.basePipelineHandle  = VK_NULL_HANDLE;
      ci.basePipelineIndex   = -1;
      vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr, &operation.pipelines[i]);
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  {
    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = operation.render_pass;
    ci.width           = static_cast<uint32_t>(desired_size[0]);
    ci.height          = static_cast<uint32_t>(desired_size[1]);
    ci.layers          = 1;
    ci.attachmentCount = SDL_arraysize(operation.cubemap_image_side_views);
    ci.pAttachments    = operation.cubemap_image_side_views;
    vkCreateFramebuffer(egh.device, &ci, nullptr, &operation.framebuffer);
  }

  // -------------------------------------------------------------------------------------

  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = egh.graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_values[6] = {};
      for (VkClearValue& clear_value : clear_values)
        clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

      VkRenderPassBeginInfo begin{};
      begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      begin.renderPass               = operation.render_pass;
      begin.framebuffer              = operation.framebuffer;
      begin.clearValueCount          = SDL_arraysize(clear_values);
      begin.pClearValues             = clear_values;
      begin.renderArea.extent.width  = static_cast<uint32_t>(desired_size[0]);
      begin.renderArea.extent.height = static_cast<uint32_t>(desired_size[1]);
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

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipelines[i]);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipeline_layout, 0, 1,
                              &operation.descriptor_set, 0, nullptr);

      vkCmdPushConstants(cmd, operation.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      game->box.renderRaw(*engine, cmd);
      if (5 != i)
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &cmd;
      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  vkDestroyFramebuffer(egh.device, operation.framebuffer, nullptr);

  for (VkImageView& image_view : operation.cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : operation.pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, operation.pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, operation.descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, operation.render_pass, nullptr);

  // original equirectangular image is no longer needed
  vkDestroyImage(egh.device, engine->images.images[plain_texture_idx], nullptr);
  vkDestroyImageView(egh.device, engine->images.image_views[plain_texture_idx], nullptr);
  engine->images.pop();
  engine->images.loaded_count -= 1;

  return result_idx;
}

int generate_irradiance_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2])
{
  struct OperationContext
  {
    VkImage     cubemap_image;
    VkImageView cubemap_image_view;
    VkImageView cubemap_image_side_views[6];

    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipelines[6];
    VkFramebuffer         framebuffer;
    VkDescriptorSet       descriptor_set;
  };

  OperationContext        operation{};
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.format        = surface_format;
    ci.extent.width  = static_cast<uint32_t>(desired_size[0]);
    ci.extent.height = static_cast<uint32_t>(desired_size[1]);
    ci.extent.depth  = 1;
    ci.mipLevels     = 1;
    ci.arrayLayers   = 6;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(egh.device, &ci, nullptr, &operation.cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, operation.cubemap_image, &reqs);
    vkBindImageMemory(egh.device, operation.cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = 0;
    sr.layerCount     = 6;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_CUBE;
    ci.subresourceRange = sr;
    ci.format           = surface_format;
    ci.image            = operation.cubemap_image;
    vkCreateImageView(egh.device, &ci, nullptr, &operation.cubemap_image_view);
  }

  for (int i = 0; i < SDL_arraysize(operation.cubemap_image_side_views); ++i)
  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = 1;
    sr.baseArrayLayer = static_cast<uint32_t>(i);
    sr.layerCount     = 1;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ci.subresourceRange = sr;
    ci.format           = surface_format;
    ci.image            = operation.cubemap_image;
    vkCreateImageView(egh.device, &ci, nullptr, &operation.cubemap_image_side_views[i]);
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(operation.cubemap_image, operation.cubemap_image_view);

  {
    VkAttachmentDescription attachments[6]{};
    for (VkAttachmentDescription& attachment : attachments)
    {
      attachment.format         = surface_format;
      attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
      attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[6]{};
    for (int i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[6]{};
    for (int i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkSubpassDependency dependencies[6]{};
    for (VkSubpassDependency& dependency : dependencies)
    {
      dependency.srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    for (int i = 1; i < 6; ++i)
    {
      dependencies[i].srcSubpass = static_cast<uint32_t>(i - 1);
      dependencies[i].dstSubpass = static_cast<uint32_t>(i);
    }

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    ci.subpassCount    = SDL_arraysize(subpasses);
    ci.pSubpasses      = subpasses;
    ci.dependencyCount = SDL_arraysize(dependencies);
    ci.pDependencies   = dependencies;

    vkCreateRenderPass(egh.device, &ci, nullptr, &operation.render_pass);
  }

  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &binding;

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &operation.descriptor_set_layout);
  }

  {
    VkDescriptorSetAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool     = egh.descriptor_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &operation.descriptor_set_layout;
    vkAllocateDescriptorSets(egh.device, &info, &operation.descriptor_set);
  }

  {
    VkDescriptorImageInfo image{};
    image.sampler     = egh.texture_sampler;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine->images.image_views[environment_cubemap_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = operation.descriptor_set;

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  {
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset     = 0;
    range.size       = 16 * sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &operation.descriptor_set_layout;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &range;
    vkCreatePipelineLayout(egh.device, &ci, nullptr, &operation.pipeline_layout);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = engine->load_shader("cubemap_to_irradiance.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = engine->load_shader("cubemap_to_irradiance.frag.spv");
    shader_stages[1].pName  = "main";

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description{};
    attribute_description.location = 0;
    attribute_description.binding  = 0;
    attribute_description.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_description.offset   = static_cast<uint32_t>(offsetof(Vertex, position));

    VkVertexInputBindingDescription vertex_binding_description{};
    vertex_binding_description.binding   = 0;
    vertex_binding_description.stride    = sizeof(Vertex);
    vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = 1;
    vertex_input_state.pVertexBindingDescriptions      = &vertex_binding_description;
    vertex_input_state.vertexAttributeDescriptionCount = 1;
    vertex_input_state.pVertexAttributeDescriptions    = &attribute_description;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(desired_size[0]);
    viewport.height   = static_cast<float>(desired_size[1]);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset        = {0, 0};
    scissor.extent.width  = static_cast<uint32_t>(desired_size[0]);
    scissor.extent.height = static_cast<uint32_t>(desired_size[1]);

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = &viewport;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable       = VK_FALSE;
    depth_stencil_state.depthWriteEnable      = VK_FALSE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.blendEnable         = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments    = &color_blend_attachment;

    for (int i = 0; i < SDL_arraysize(operation.pipelines); ++i)
    {
      VkGraphicsPipelineCreateInfo ci{};
      ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      ci.stageCount          = SDL_arraysize(shader_stages);
      ci.pStages             = shader_stages;
      ci.pVertexInputState   = &vertex_input_state;
      ci.pInputAssemblyState = &input_assembly_state;
      ci.pViewportState      = &viewport_state;
      ci.pRasterizationState = &rasterization_state;
      ci.pMultisampleState   = &multisample_state;
      ci.pDepthStencilState  = &depth_stencil_state;
      ci.pColorBlendState    = &color_blend_state;
      ci.layout              = operation.pipeline_layout;
      ci.renderPass          = operation.render_pass;
      ci.subpass             = static_cast<uint32_t>(i);
      ci.basePipelineHandle  = VK_NULL_HANDLE;
      ci.basePipelineIndex   = -1;
      vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr, &operation.pipelines[i]);
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  {
    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = operation.render_pass;
    ci.width           = static_cast<uint32_t>(desired_size[0]);
    ci.height          = static_cast<uint32_t>(desired_size[1]);
    ci.layers          = 1;
    ci.attachmentCount = SDL_arraysize(operation.cubemap_image_side_views);
    ci.pAttachments    = operation.cubemap_image_side_views;
    vkCreateFramebuffer(egh.device, &ci, nullptr, &operation.framebuffer);
  }

  // -------------------------------------------------------------------------------------
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = egh.graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_values[6] = {};
      for (VkClearValue& clear_value : clear_values)
        clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

      VkRenderPassBeginInfo begin{};
      begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      begin.renderPass               = operation.render_pass;
      begin.framebuffer              = operation.framebuffer;
      begin.clearValueCount          = SDL_arraysize(clear_values);
      begin.pClearValues             = clear_values;
      begin.renderArea.extent.width  = static_cast<uint32_t>(desired_size[0]);
      begin.renderArea.extent.height = static_cast<uint32_t>(desired_size[1]);
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

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipelines[i]);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipeline_layout, 0, 1,
                              &operation.descriptor_set, 0, nullptr);
      vkCmdPushConstants(cmd, operation.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
      game->box.renderRaw(*engine, cmd);

      if (5 != i)
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &cmd;
      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  vkDestroyFramebuffer(egh.device, operation.framebuffer, nullptr);

  for (VkImageView& image_view : operation.cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : operation.pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, operation.pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, operation.descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, operation.render_pass, nullptr);

  return result_idx;
}

int generate_prefiltered_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2])
{
  enum
  {
    CUBE_SIDES         = 6,
    DESIRED_MIP_LEVELS = 5
  };

  struct OperationContext
  {
    VkImage     cubemap_image;
    VkImageView cubemap_image_view;
    VkImageView cubemap_image_side_views[CUBE_SIDES * DESIRED_MIP_LEVELS];

    VkRenderPass          render_pass;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipelines[CUBE_SIDES * DESIRED_MIP_LEVELS];
    VkFramebuffer         framebuffers[DESIRED_MIP_LEVELS];
    VkDescriptorSet       descriptor_set;
  };

  OperationContext        operation{};
  Engine::GenericHandles& egh            = engine->generic_handles;
  const VkFormat          surface_format = engine->generic_handles.surface_format.format;

  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.format        = surface_format;
    ci.extent.width  = static_cast<uint32_t>(desired_size[0]);
    ci.extent.height = static_cast<uint32_t>(desired_size[1]);
    ci.extent.depth  = 1;
    ci.mipLevels     = DESIRED_MIP_LEVELS;
    ci.arrayLayers   = CUBE_SIDES;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    vkCreateImage(egh.device, &ci, nullptr, &operation.cubemap_image);
  }

  {
    VkMemoryRequirements reqs = {};
    vkGetImageMemoryRequirements(egh.device, operation.cubemap_image, &reqs);
    vkBindImageMemory(egh.device, operation.cubemap_image, engine->images.memory, engine->images.allocate(reqs.size));
  }

  {
    VkImageSubresourceRange sr{};
    sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    sr.baseMipLevel   = 0;
    sr.levelCount     = DESIRED_MIP_LEVELS;
    sr.baseArrayLayer = 0;
    sr.layerCount     = CUBE_SIDES;

    VkImageViewCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.viewType         = VK_IMAGE_VIEW_TYPE_CUBE;
    ci.subresourceRange = sr;
    ci.format           = surface_format;
    ci.image            = operation.cubemap_image;
    vkCreateImageView(egh.device, &ci, nullptr, &operation.cubemap_image_view);
  }

  for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
  {
    for (int cube_side = 0; cube_side < CUBE_SIDES; ++cube_side)
    {
      VkImageSubresourceRange sr{};
      sr.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      sr.baseMipLevel   = static_cast<uint32_t>(mip_level);
      sr.levelCount     = 1;
      sr.baseArrayLayer = static_cast<uint32_t>(cube_side);
      sr.layerCount     = 1;

      VkImageViewCreateInfo ci{};
      ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
      ci.subresourceRange = sr;
      ci.format           = surface_format;
      ci.image            = operation.cubemap_image;
      vkCreateImageView(egh.device, &ci, nullptr,
                        &operation.cubemap_image_side_views[CUBE_SIDES * mip_level + cube_side]);
    }
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(operation.cubemap_image, operation.cubemap_image_view);

  {
    VkAttachmentDescription attachments[CUBE_SIDES]{};
    for (VkAttachmentDescription& attachment : attachments)
    {
      attachment.format         = surface_format;
      attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
      attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
      attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference color_references[CUBE_SIDES]{};
    for (int i = 0; i < SDL_arraysize(color_references); ++i)
    {
      color_references[i].attachment = static_cast<uint32_t>(i);
      color_references[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpasses[CUBE_SIDES]{};
    for (int i = 0; i < SDL_arraysize(subpasses); ++i)
    {
      subpasses[i].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpasses[i].colorAttachmentCount = 1;
      subpasses[i].pColorAttachments    = &color_references[i];
    }

    VkSubpassDependency dependencies[CUBE_SIDES]{};
    for (VkSubpassDependency& dependency : dependencies)
    {
      dependency.srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    for (int i = 1; i < CUBE_SIDES; ++i)
    {
      dependencies[i].srcSubpass = static_cast<uint32_t>(i - 1);
      dependencies[i].dstSubpass = static_cast<uint32_t>(i);
    }

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    ci.subpassCount    = SDL_arraysize(subpasses);
    ci.pSubpasses      = subpasses;
    ci.dependencyCount = SDL_arraysize(dependencies);
    ci.pDependencies   = dependencies;

    vkCreateRenderPass(egh.device, &ci, nullptr, &operation.render_pass);
  }

  {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &binding;

    vkCreateDescriptorSetLayout(egh.device, &ci, nullptr, &operation.descriptor_set_layout);
  }

  {
    VkDescriptorSetAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool     = egh.descriptor_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &operation.descriptor_set_layout;
    vkAllocateDescriptorSets(egh.device, &info, &operation.descriptor_set);
  }

  {
    VkDescriptorImageInfo image{};
    image.sampler     = egh.texture_sampler;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = engine->images.image_views[environment_cubemap_idx];

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &image;
    write.dstSet          = operation.descriptor_set;

    vkUpdateDescriptorSets(egh.device, 1, &write, 0, nullptr);
  }

  {
    VkPushConstantRange ranges[2]{};
    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = sizeof(mat4x4);

    ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ranges[1].offset     = sizeof(mat4x4);
    ranges[1].size       = sizeof(float);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &operation.descriptor_set_layout;
    ci.pushConstantRangeCount = 2;
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(egh.device, &ci, nullptr, &operation.pipeline_layout);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = engine->load_shader("cubemap_prefiltering.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = engine->load_shader("cubemap_prefiltering.frag.spv");
    shader_stages[1].pName  = "main";

    struct Vertex
    {
      float position[3];
      float pad[5];
    };

    VkVertexInputAttributeDescription attribute_description{};
    attribute_description.location = 0;
    attribute_description.binding  = 0;
    attribute_description.format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_description.offset   = static_cast<uint32_t>(offsetof(Vertex, position));

    VkVertexInputBindingDescription vertex_binding_description{};
    vertex_binding_description.binding   = 0;
    vertex_binding_description.stride    = sizeof(Vertex);
    vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = 1;
    vertex_input_state.pVertexBindingDescriptions      = &vertex_binding_description;
    vertex_input_state.vertexAttributeDescriptionCount = 1;
    vertex_input_state.pVertexAttributeDescriptions    = &attribute_description;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable         = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp          = 0.0f;
    rasterization_state.depthBiasSlopeFactor    = 0.0f;
    rasterization_state.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state{};
    multisample_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable   = VK_FALSE;
    multisample_state.minSampleShading      = 1.0f;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable      = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    depth_stencil_state.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable       = VK_FALSE;
    depth_stencil_state.depthWriteEnable      = VK_FALSE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.blendEnable         = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments    = &color_blend_attachment;

    for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
    {
      VkViewport viewport{};
      viewport.x        = 0.0f;
      viewport.y        = 0.0f;
      viewport.width    = static_cast<float>(desired_size[0]) / calculate_mip_divisor(mip_level);
      viewport.height   = static_cast<float>(desired_size[1]) / calculate_mip_divisor(mip_level);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;

      VkRect2D scissor{};
      scissor.offset        = {0, 0};
      scissor.extent.width  = static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level);
      scissor.extent.height = static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level);

      VkPipelineViewportStateCreateInfo viewport_state{};
      viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
      viewport_state.viewportCount = 1;
      viewport_state.pViewports    = &viewport;
      viewport_state.scissorCount  = 1;
      viewport_state.pScissors     = &scissor;

      for (int cube_side = 0; cube_side < CUBE_SIDES; ++cube_side)
      {
        VkGraphicsPipelineCreateInfo ci{};
        ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount          = SDL_arraysize(shader_stages);
        ci.pStages             = shader_stages;
        ci.pVertexInputState   = &vertex_input_state;
        ci.pInputAssemblyState = &input_assembly_state;
        ci.pViewportState      = &viewport_state;
        ci.pRasterizationState = &rasterization_state;
        ci.pMultisampleState   = &multisample_state;
        ci.pDepthStencilState  = &depth_stencil_state;
        ci.pColorBlendState    = &color_blend_state;
        ci.layout              = operation.pipeline_layout;
        ci.renderPass          = operation.render_pass;
        ci.subpass             = static_cast<uint32_t>(cube_side);
        ci.basePipelineHandle  = VK_NULL_HANDLE;
        ci.basePipelineIndex   = -1;
        vkCreateGraphicsPipelines(egh.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                  &operation.pipelines[CUBE_SIDES * mip_level + cube_side]);
      }
    }

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(egh.device, shader_stage.module, nullptr);
  }

  for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
  {
    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = operation.render_pass;
    ci.width           = static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level);
    ci.height          = static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level);
    ci.layers          = 1;
    ci.attachmentCount = CUBE_SIDES;
    ci.pAttachments    = &operation.cubemap_image_side_views[CUBE_SIDES * mip_level];

    vkCreateFramebuffer(egh.device, &ci, nullptr, &operation.framebuffers[mip_level]);
  }

  // -------------------------------------------------------------------------------------
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = egh.graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      vkAllocateCommandBuffers(egh.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &begin);
    }

    for (int mip_level = 0; mip_level < DESIRED_MIP_LEVELS; ++mip_level)
    {
      {
        VkClearValue clear_values[CUBE_SIDES] = {};
        for (VkClearValue& clear_value : clear_values)
          clear_value.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        VkRenderPassBeginInfo begin{};
        begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass      = operation.render_pass;
        begin.framebuffer     = operation.framebuffers[mip_level];
        begin.clearValueCount = SDL_arraysize(clear_values);
        begin.pClearValues    = clear_values;
        begin.renderArea.extent.width =
            static_cast<uint32_t>(desired_size[0]) / (uint32_t)calculate_mip_divisor(mip_level);
        begin.renderArea.extent.height =
            static_cast<uint32_t>(desired_size[1]) / (uint32_t)calculate_mip_divisor(mip_level);
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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          operation.pipelines[CUBE_SIDES * mip_level + cube_side]);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipeline_layout, 0, 1,
                                &operation.descriptor_set, 0, nullptr);
        vkCmdPushConstants(cmd, operation.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), mvp);
        vkCmdPushConstants(cmd, operation.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(float),
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
      VkFenceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(egh.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &cmd;
      vkQueueSubmit(egh.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(egh.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(egh.device, image_generation_fence, nullptr);
  }

  // todo: this is a leaked resource. We should destroy this at this point, but the pool must be correctly configured
  // vkFreeDescriptorSets(egh.device, egh.descriptor_pool, 1, &operation.descriptor_set);

  for (VkFramebuffer& framebuffer : operation.framebuffers)
    vkDestroyFramebuffer(egh.device, framebuffer, nullptr);

  for (VkImageView& image_view : operation.cubemap_image_side_views)
    vkDestroyImageView(egh.device, image_view, nullptr);

  for (VkPipeline& pipeline : operation.pipelines)
    vkDestroyPipeline(egh.device, pipeline, nullptr);

  vkDestroyPipelineLayout(egh.device, operation.pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(egh.device, operation.descriptor_set_layout, nullptr);
  vkDestroyRenderPass(egh.device, operation.render_pass, nullptr);

  return result_idx;
}

int generate_brdf_lookup(Engine* engine, int size)
{
  struct OperationContext
  {
    VkImage     brdf_image;
    VkImageView brdf_image_view;

    VkRenderPass          render_pass;
    VkFramebuffer         framebuffer;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout      pipeline_layout;
    VkPipeline            pipeline;
  } operation{};

  {
    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = VK_FORMAT_R16G16_SFLOAT;
    info.extent.width  = static_cast<uint32_t>(size);
    info.extent.height = static_cast<uint32_t>(size);
    info.extent.depth  = 1;
    info.mipLevels     = 1;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkCreateImage(engine->generic_handles.device, &info, nullptr, &operation.brdf_image);
  }

  {
    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(engine->generic_handles.device, operation.brdf_image, &reqs);
    vkBindImageMemory(engine->generic_handles.device, operation.brdf_image, engine->images.memory,
                      engine->images.allocate(reqs.size));
  }

  {
    VkImageSubresourceRange range{};
    range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel   = 0;
    range.levelCount     = 1;
    range.baseArrayLayer = 0;
    range.layerCount     = 1;

    VkImageViewCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    info.format           = VK_FORMAT_R16G16_SFLOAT;
    info.subresourceRange = range;
    info.image            = operation.brdf_image;

    vkCreateImageView(engine->generic_handles.device, &info, nullptr, &operation.brdf_image_view);
  }

  int result_idx = engine->images.loaded_count;
  engine->images.add(operation.brdf_image, operation.brdf_image_view);

  {
    VkAttachmentDescription attachment{};
    attachment.format         = VK_FORMAT_R16G16_SFLOAT;
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference reference{};
    reference.attachment = 0;
    reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass_description{};
    subpass_description.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.colorAttachmentCount = 1;
    subpass_description.pColorAttachments    = &reference;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass      = 0;
    dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass      = 0;
    dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo create{};
    create.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create.attachmentCount = 1;
    create.pAttachments    = &attachment;
    create.subpassCount    = 1;
    create.pSubpasses      = &subpass_description;
    create.dependencyCount = 2;
    create.pDependencies   = dependencies;

    vkCreateRenderPass(engine->generic_handles.device, &create, nullptr, &operation.render_pass);
  }

  {
    VkFramebufferCreateInfo create{};
    create.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create.renderPass      = operation.render_pass;
    create.attachmentCount = 1;
    create.pAttachments    = &operation.brdf_image_view;
    create.width           = static_cast<uint32_t>(size);
    create.height          = static_cast<uint32_t>(size);
    create.layers          = 1;

    vkCreateFramebuffer(engine->generic_handles.device, &create, nullptr, &operation.framebuffer);
  }

  {
    VkDescriptorSetLayoutCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vkCreateDescriptorSetLayout(engine->generic_handles.device, &create, nullptr, &operation.descriptor_set_layout);
  }

  {
    VkPipelineLayoutCreateInfo create{};
    create.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create.setLayoutCount = 1;
    create.pSetLayouts    = &operation.descriptor_set_layout;
    vkCreatePipelineLayout(engine->generic_handles.device, &create, nullptr, &operation.pipeline_layout);
  }

  {
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode    = VK_CULL_MODE_NONE;
    rasterization_state.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.lineWidth   = 1.0f;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend_attachment;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = VK_FALSE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil.front            = depth_stencil.back;
    depth_stencil.back.compareOp   = VK_COMPARE_OP_ALWAYS;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates    = dynamic_states;
    dynamicStateCI.dynamicStateCount = SDL_arraysize(dynamic_states);

    VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
    emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = engine->load_shader("brdf_compute.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = engine->load_shader("brdf_compute.frag.spv");
    shader_stages[1].pName  = "main";

    VkGraphicsPipelineCreateInfo create{};
    create.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create.layout              = operation.pipeline_layout;
    create.renderPass          = operation.render_pass;
    create.pInputAssemblyState = &input_assembly;
    create.pVertexInputState   = &emptyInputStateCI;
    create.pRasterizationState = &rasterization_state;
    create.pColorBlendState    = &color_blend;
    create.pMultisampleState   = &multisampling;
    create.pViewportState      = &viewport;
    create.pDepthStencilState  = &depth_stencil;
    create.pDynamicState       = &dynamicStateCI;
    create.stageCount          = 2;
    create.pStages             = shader_stages;

    vkCreateGraphicsPipelines(engine->generic_handles.device, VK_NULL_HANDLE, 1, &create, nullptr, &operation.pipeline);
    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(engine->generic_handles.device, shader_stage.module, nullptr);
  }

  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = engine->generic_handles.graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      vkAllocateCommandBuffers(engine->generic_handles.device, &allocate, &cmd);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &begin);
    }

    {
      VkClearValue clear_value{};
      clear_value.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

      VkRenderPassBeginInfo info{};
      info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      info.renderPass               = operation.render_pass;
      info.renderArea.extent.width  = static_cast<uint32_t>(size);
      info.renderArea.extent.height = static_cast<uint32_t>(size);
      info.clearValueCount          = 1;
      info.pClearValues             = &clear_value;
      info.framebuffer              = operation.framebuffer;
      vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    VkViewport viewport{};
    viewport.width    = (float)size;
    viewport.height   = (float)size;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent.width  = static_cast<uint32_t>(size);
    scissor.extent.height = static_cast<uint32_t>(size);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, operation.pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFence image_generation_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(engine->generic_handles.device, &ci, nullptr, &image_generation_fence);
    }

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &cmd;
      vkQueueSubmit(engine->generic_handles.graphics_queue, 1, &submit, image_generation_fence);
    }

    vkWaitForFences(engine->generic_handles.device, 1, &image_generation_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(engine->generic_handles.device, image_generation_fence, nullptr);
  }

  vkDestroyPipeline(engine->generic_handles.device, operation.pipeline, nullptr);
  vkDestroyPipelineLayout(engine->generic_handles.device, operation.pipeline_layout, nullptr);
  vkDestroyDescriptorSetLayout(engine->generic_handles.device, operation.descriptor_set_layout, nullptr);
  vkDestroyFramebuffer(engine->generic_handles.device, operation.framebuffer, nullptr);
  vkDestroyRenderPass(engine->generic_handles.device, operation.render_pass, nullptr);

  return result_idx;
}
