#include "engine.hh"
#include <linmath.h>

namespace {

struct TrianglesVertex
{
  float position[3];
  float normal[3];
  float tex_coord[2];
};

struct ImguiVertex
{
  float    position[2];
  float    tex_coord[2];
  uint32_t color;
};

constexpr float to_rad(float deg) noexcept
{
  return (float(M_PI) * deg) / 180.0f;
}

void generate(CubeBuffer& buffer)
{
  // todo: optimize in the future maybe
  for (int i = 0; i < 36; ++i)
  {
    uint32_t cube_side_index_offsets[] = {0, 1, 2, 2, 3, 0};
    int      start_index               = (i / 6) * 4;
    uint32_t cube_side_index_offset    = cube_side_index_offsets[i % 6];
    buffer.indices[i]                  = (uint32_t)start_index + cube_side_index_offset;
  }

  for (int i = 0; i < 24; ++i)
  {
    struct
    {
      vec4 position;
      vec4 normal;
      vec2 tex_coord;
    } references[] = {
        {{-1.0f, -1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, 1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    };

    int    current_reference = i % 4;
    int    current_side      = i / 4;
    vec4   position          = {};
    vec4   normal            = {};
    mat4x4 rotation          = {};

    mat4x4_identity(rotation);

    switch (current_side)
    {
    default: // sides
      mat4x4_rotate_Y(rotation, rotation, to_rad(90.0f) * (i / 4));
      break;
    case 4: // top
      mat4x4_rotate_X(rotation, rotation, to_rad(90.0f));
      break;
    case 5: // bottom
      mat4x4_rotate_X(rotation, rotation, to_rad(-90.0f));
      break;
    }

    mat4x4_mul_vec4(position, rotation, references[current_reference].position);
    mat4x4_mul_vec4(normal, rotation, references[current_reference].normal);

    SDL_memcpy(buffer.vertices[i].position, position, 3 * sizeof(float));
    SDL_memcpy(buffer.vertices[i].normal, normal, 3 * sizeof(float));
    SDL_memcpy(buffer.vertices[i].tex_coord, references[current_reference].tex_coord, 2 * sizeof(float));
  }
}

} // namespace

void Engine::renderer_simple()
{
  Engine::SimpleRenderer& renderer = simple_renderer;

  {
    VkAttachmentDescription attachments[2] = {};

    attachments[0].format         = surface_format.format;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_reference{};
    depth_reference.attachment = 1;
    depth_reference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpasses[2] = {};

    subpasses[0].pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount    = 1;
    subpasses[0].pColorAttachments       = &color_reference;
    subpasses[0].pDepthStencilAttachment = &depth_reference;

    subpasses[1].pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments    = &color_reference;

    VkSubpassDependency dependencies[2] = {};

    dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass    = 0;
    dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass    = 0;
    dependencies[1].dstSubpass    = 1;
    dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    ci.subpassCount    = SDL_arraysize(subpasses);
    ci.pSubpasses      = subpasses;
    ci.dependencyCount = SDL_arraysize(dependencies);
    ci.pDependencies   = dependencies;

    vkCreateRenderPass(device, &ci, nullptr, &renderer.render_pass);
  }

  {
    VkDescriptorSetLayoutBinding bindings[1] = {};

    bindings[0].binding         = 1;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = SDL_arraysize(bindings);
    ci.pBindings    = bindings;

    for (VkDescriptorSetLayout& layout : renderer.descriptor_set_layouts)
      vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout);
  }

  {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool     = descriptor_pool;
    allocate.descriptorSetCount = SDL_arraysize(renderer.descriptor_sets);
    allocate.pSetLayouts        = renderer.descriptor_set_layouts;

    vkAllocateDescriptorSets(device, &allocate, renderer.descriptor_sets);
  }

  {
    VkPushConstantRange ranges[1] = {};

    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = sizeof(float[4][4]);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = SWAPCHAIN_IMAGES_COUNT;
    ci.pSetLayouts            = renderer.descriptor_set_layouts;
    ci.pushConstantRangeCount = SDL_arraysize(ranges);
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(device, &ci, nullptr, &renderer.pipeline_layouts[0]);
  }

  {
    VkPushConstantRange ranges[1] = {};

    ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ranges[0].offset     = 0;
    ranges[0].size       = sizeof(float[4][4]);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = SWAPCHAIN_IMAGES_COUNT;
    ci.pSetLayouts            = renderer.descriptor_set_layouts;
    ci.pushConstantRangeCount = SDL_arraysize(ranges);
    ci.pPushConstantRanges    = ranges;
    vkCreatePipelineLayout(device, &ci, nullptr, &renderer.pipeline_layouts[1]);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = load_shader("triangle_push.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = load_shader("triangle_push.frag.spv");
    shader_stages[1].pName  = "main";

    VkVertexInputAttributeDescription attribute_descriptions[3] = {};

    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, position));

    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[1].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, normal));

    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[2].offset   = static_cast<uint32_t>(offsetof(TrianglesVertex, tex_coord));

    VkVertexInputBindingDescription vertex_binding_descriptions[1] = {};

    vertex_binding_descriptions[0].binding   = 0;
    vertex_binding_descriptions[0].stride    = sizeof(TrianglesVertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions);
    vertex_input_state.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state.vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions);
    vertex_input_state.pVertexAttributeDescriptions    = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewports[1] = {};

    viewports[0].x        = 0.0f;
    viewports[0].y        = 0.0f;
    viewports[0].width    = static_cast<float>(extent2D.width);
    viewports[0].height   = static_cast<float>(extent2D.height);
    viewports[0].minDepth = 0.0f;
    viewports[0].maxDepth = 1.0f;

    VkRect2D scissors[1] = {};

    scissors[0].offset = {0, 0};
    scissors[0].extent = extent2D;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = SDL_arraysize(viewports);
    viewport_state.pViewports    = viewports;
    viewport_state.scissorCount  = SDL_arraysize(scissors);
    viewport_state.pScissors     = scissors;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_CLOCKWISE;
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
    depth_stencil_state.depthTestEnable       = VK_TRUE;
    depth_stencil_state.depthWriteEnable      = VK_TRUE;
    depth_stencil_state.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable     = VK_FALSE;
    depth_stencil_state.minDepthBounds        = 0.0f;
    depth_stencil_state.maxDepthBounds        = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};

    color_blend_attachments[0].blendEnable         = VK_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = SDL_arraysize(color_blend_attachments);
    color_blend_state.pAttachments    = color_blend_attachments;

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
    ci.layout              = renderer.pipeline_layouts[0];
    ci.renderPass          = renderer.render_pass;
    ci.subpass             = 0;
    ci.basePipelineHandle  = VK_NULL_HANDLE;
    ci.basePipelineIndex   = -1;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &renderer.pipelines[0]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(device, shader_stage.module, nullptr);
  }

  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};

    shader_stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = load_shader("imgui.vert.spv");
    shader_stages[0].pName  = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = load_shader("imgui.frag.spv");
    shader_stages[1].pName  = "main";

    VkVertexInputAttributeDescription attribute_descriptions[3] = {};

    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[0].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, position));

    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[1].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, tex_coord));

    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].format   = VK_FORMAT_R8G8B8A8_UNORM;
    attribute_descriptions[2].offset   = static_cast<uint32_t>(offsetof(ImguiVertex, color));

    VkVertexInputBindingDescription vertex_binding_descriptions[1] = {};

    vertex_binding_descriptions[0].binding   = 0;
    vertex_binding_descriptions[0].stride    = sizeof(ImguiVertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount   = SDL_arraysize(vertex_binding_descriptions);
    vertex_input_state.pVertexBindingDescriptions      = vertex_binding_descriptions;
    vertex_input_state.vertexAttributeDescriptionCount = SDL_arraysize(attribute_descriptions);
    vertex_input_state.pVertexAttributeDescriptions    = attribute_descriptions;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
    input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewports[1] = {};

    viewports[0].x        = 0.0f;
    viewports[0].y        = 0.0f;
    viewports[0].width    = static_cast<float>(extent2D.width);
    viewports[0].height   = static_cast<float>(extent2D.height);
    viewports[0].minDepth = 0.0f;
    viewports[0].maxDepth = 1.0f;

    VkRect2D scissors[1] = {};

    scissors[0].offset = {0, 0};
    scissors[0].extent = extent2D;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = SDL_arraysize(viewports);
    viewport_state.pViewports    = viewports;
    viewport_state.scissorCount  = SDL_arraysize(scissors);
    viewport_state.pScissors     = scissors;

    VkPipelineRasterizationStateCreateInfo rasterization_state{};
    rasterization_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.depthClampEnable        = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace               = VK_FRONT_FACE_CLOCKWISE;
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

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};

    color_blend_attachments[0].blendEnable         = VK_TRUE;
    color_blend_attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state{};
    color_blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.logicOpEnable   = VK_FALSE;
    color_blend_state.logicOp         = VK_LOGIC_OP_COPY;
    color_blend_state.attachmentCount = SDL_arraysize(color_blend_attachments);
    color_blend_state.pAttachments    = color_blend_attachments;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = SDL_arraysize(dynamic_states);
    dynamic_state.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount          = SDL_arraysize(shader_stages);
    ci.pStages             = shader_stages;
    ci.pVertexInputState   = &vertex_input_state;
    ci.pInputAssemblyState = &input_assembly_state;
    ci.pViewportState      = &viewport_state;
    ci.pRasterizationState = &rasterization_state;
    ci.pMultisampleState   = &multisample_state;
    ci.pColorBlendState    = &color_blend_state;
    ci.pDynamicState       = &dynamic_state;
    ci.layout              = renderer.pipeline_layouts[1];
    ci.renderPass          = renderer.render_pass;
    ci.subpass             = 1;
    ci.basePipelineHandle  = VK_NULL_HANDLE;
    ci.basePipelineIndex   = -1;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &renderer.pipelines[1]);

    for (auto& shader_stage : shader_stages)
      vkDestroyShaderModule(device, shader_stage.module, nullptr);
  }

  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments[] = {swapchain_image_views[i], depth_image_view};

    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = renderer.render_pass;
    ci.width           = extent2D.width;
    ci.height          = extent2D.height;
    ci.layers          = 1;
    ci.attachmentCount = SDL_arraysize(attachments);
    ci.pAttachments    = attachments;
    vkCreateFramebuffer(device, &ci, nullptr, &renderer.framebuffers[i]);
  }

  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkFenceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &ci, nullptr, &renderer.submition_fences[i]);
  }

  // -----------------------------------------------------
  //                    CUBE BUFFERS
  // -----------------------------------------------------
  {
    VkBuffer host_buffer = VK_NULL_HANDLE;
    {
      VkBufferCreateInfo ci{};
      ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      ci.size        = sizeof(CubeBuffer);
      ci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      vkCreateBuffer(device, &ci, nullptr, &host_buffer);
    }

    {
      VkBufferCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      ci.size  = sizeof(CubeBuffer);
      ci.usage =
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      vkCreateBuffer(device, &ci, nullptr, &renderer.scene.cube_buffer);
    }

    VkDeviceMemory host_memory = VK_NULL_HANDLE;
    {
      VkMemoryRequirements reqs = {};
      vkGetBufferMemoryRequirements(device, host_buffer, &reqs);

      VkPhysicalDeviceMemoryProperties properties = {};
      vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

      VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

      VkMemoryAllocateInfo allocate{};
      allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocate.allocationSize  = reqs.size;
      allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, flags);

      vkAllocateMemory(device, &allocate, nullptr, &host_memory);
      vkBindBufferMemory(device, host_buffer, host_memory, 0);
    }

    {
      VkMemoryRequirements reqs = {};
      vkGetBufferMemoryRequirements(device, renderer.scene.cube_buffer, &reqs);

      VkPhysicalDeviceMemoryProperties properties = {};
      vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

      VkMemoryAllocateInfo allocate{};
      allocate.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocate.allocationSize  = reqs.size;
      allocate.memoryTypeIndex = find_memory_type_index(&properties, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      vkAllocateMemory(device, &allocate, nullptr, &renderer.scene.cube_buffer_memory);
      vkBindBufferMemory(device, renderer.scene.cube_buffer, renderer.scene.cube_buffer_memory, 0);
    }

    CubeBuffer* static_data_mapped_gpu = nullptr;
    vkMapMemory(device, host_memory, 0, sizeof(CubeBuffer), 0, (void**)&static_data_mapped_gpu);
    generate(*static_data_mapped_gpu);
    vkUnmapMemory(device, host_memory);

    VkCommandBuffer copy_command = VK_NULL_HANDLE;
    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &allocate, &copy_command);
    }

    {
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(copy_command, &begin);
    }

    {
      VkBufferCopy copy{};
      copy.size = sizeof(CubeBuffer);
      vkCmdCopyBuffer(copy_command, host_buffer, renderer.scene.cube_buffer, 1, &copy);
    }

    {
      VkBufferMemoryBarrier barrier{};
      barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.buffer              = renderer.scene.cube_buffer;
      barrier.offset              = 0;
      barrier.size                = sizeof(CubeBuffer);
      vkCmdPipelineBarrier(copy_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0,
                           nullptr, 1, &barrier, 0, nullptr);
    }

    vkEndCommandBuffer(copy_command);

    VkFence data_upload_fence = VK_NULL_HANDLE;
    {
      VkFenceCreateInfo ci{};
      ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(device, &ci, nullptr, &data_upload_fence);
    }

    {
      VkSubmitInfo submit{};
      submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers    = &copy_command;
      vkQueueSubmit(graphics_queue, 1, &submit, data_upload_fence);
    }

    vkWaitForFences(device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, data_upload_fence, nullptr);
    vkFreeCommandBuffers(device, graphics_command_pool, 1, &copy_command);

    vkDestroyBuffer(device, host_buffer, nullptr);
    vkFreeMemory(device, host_memory, nullptr);

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
      allocate.commandBufferCount = SWAPCHAIN_IMAGES_COUNT;
      vkAllocateCommandBuffers(device, &allocate, renderer.scene.secondary_command_buffers);
      vkAllocateCommandBuffers(device, &allocate, renderer.gui.secondary_command_buffers);
    }

    {
      VkCommandBufferAllocateInfo allocate{};
      allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocate.commandPool        = graphics_command_pool;
      allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocate.commandBufferCount = SWAPCHAIN_IMAGES_COUNT;
      vkAllocateCommandBuffers(device, &allocate, renderer.primary_command_buffers);
    }
  }
}
