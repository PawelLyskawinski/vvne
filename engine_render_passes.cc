#include "engine.hh"

namespace {

void shadowmap(Engine& engine)
{
  VkAttachmentDescription attachment = {
      .format         = VK_FORMAT_D32_SFLOAT,
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkAttachmentReference depth_reference = {
      .attachment = 0,
      .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass = {
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .pDepthStencilAttachment = &depth_reference,
  };

  VkSubpassDependency dependencies[] = {
      {
          .srcSubpass    = VK_SUBPASS_EXTERNAL,
          .dstSubpass    = 0,
          .srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          .srcAccessMask = 0,
          .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      },
      {
          .srcSubpass    = 0,
          .dstSubpass    = VK_SUBPASS_EXTERNAL,
          .srcStageMask  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      },
  };

  VkRenderPassCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments    = &attachment,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
      .dependencyCount = SDL_arraysize(dependencies),
      .pDependencies   = dependencies,
  };

  vkCreateRenderPass(engine.device, &ci, nullptr, &engine.shadowmap_render_pass);
}

void skybox(Engine& engine)
{
  VkAttachmentDescription attachments_msaa[] = {
      {
          .format         = engine.surface_format.format,
          .samples        = VK_SAMPLE_COUNT_1_BIT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
      {
          .format         = engine.surface_format.format,
          .samples        = engine.MSAA_SAMPLE_COUNT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkAttachmentDescription attachments_no_msaa[] = {attachments_msaa[0]};

  VkAttachmentReference references[] = {
      {
          .attachment = 0,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
      {
          .attachment = 1,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkSubpassDescription subpass_msaa = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments    = &references[1],
      .pResolveAttachments  = &references[0],
  };

  VkSubpassDescription subpass_no_msaa = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments    = &references[0],
  };

  VkSubpassDependency dependencies[] = {
      {
          .srcSubpass    = VK_SUBPASS_EXTERNAL,
          .dstSubpass    = 0,
          .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      },
      {
          .srcSubpass    = 0,
          .dstSubpass    = VK_SUBPASS_EXTERNAL,
          .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      },
  };

  VkRenderPassCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .subpassCount    = 1,
      .dependencyCount = SDL_arraysize(dependencies),
      .pDependencies   = dependencies,
  };

  if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
  {
    ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
    ci.pAttachments    = attachments_no_msaa;
    ci.pSubpasses      = &subpass_no_msaa;
  }
  else
  {
    ci.attachmentCount = SDL_arraysize(attachments_msaa);
    ci.pAttachments    = attachments_msaa;
    ci.pSubpasses      = &subpass_msaa;
  }

  vkCreateRenderPass(engine.device, &ci, nullptr, &engine.skybox_render_pass);
}

void color_and_depth(Engine& engine)
{
  VkAttachmentDescription attachments_msaa[] = {
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
      {
          .format         = VK_FORMAT_D32_SFLOAT,
          .samples        = engine.MSAA_SAMPLE_COUNT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      },
      {
          .format         = engine.surface_format.format,
          .samples        = engine.MSAA_SAMPLE_COUNT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkAttachmentDescription attachments_no_msaa[] = {
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
      {
          .format         = VK_FORMAT_D32_SFLOAT,
          .samples        = VK_SAMPLE_COUNT_1_BIT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      },
  };

  VkAttachmentReference references[] = {
      {
          .attachment = 0,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
      {
          .attachment = 1,
          .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      },
      {
          .attachment = 2,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkSubpassDescription subpass_msaa = {
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount    = 1,
      .pColorAttachments       = &references[2],
      .pResolveAttachments     = &references[0],
      .pDepthStencilAttachment = &references[1],
  };

  VkSubpassDescription subpass_no_msaa = {
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount    = 1,
      .pColorAttachments       = &references[0],
      .pDepthStencilAttachment = &references[1],
  };

  VkSubpassDependency dependencies[] = {
      {
          .srcSubpass    = VK_SUBPASS_EXTERNAL,
          .dstSubpass    = 0,
          .srcStageMask  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      },
      {
          .srcSubpass    = 0,
          .dstSubpass    = VK_SUBPASS_EXTERNAL,
          .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .dstAccessMask = 0,
      },
  };

  VkRenderPassCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .subpassCount    = 1,
      .dependencyCount = SDL_arraysize(dependencies),
      .pDependencies   = dependencies,
  };

  if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
  {
    ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
    ci.pAttachments    = attachments_no_msaa;
    ci.pSubpasses      = &subpass_no_msaa;
  }
  else
  {
    ci.attachmentCount = SDL_arraysize(attachments_msaa);
    ci.pAttachments    = attachments_msaa;
    ci.pSubpasses      = &subpass_msaa;
  }

  vkCreateRenderPass(engine.device, &ci, nullptr, &engine.color_and_depth_render_pass);
}

void gui(Engine& engine)
{
  VkAttachmentDescription attachments_msaa[] = {
      {
          .format         = engine.surface_format.format,
          .samples        = VK_SAMPLE_COUNT_1_BIT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      },
      {
          .format         = engine.surface_format.format,
          .samples        = engine.MSAA_SAMPLE_COUNT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkAttachmentDescription attachments_no_msaa[] = {
      {
          .format         = engine.surface_format.format,
          .samples        = VK_SAMPLE_COUNT_1_BIT,
          .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      },
  };

  VkAttachmentReference references[] = {
      {
          .attachment = 0,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
      {
          .attachment = 1,
          .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
  };

  VkSubpassDescription subpass_msaa = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments    = &references[1],
      .pResolveAttachments  = &references[0],
  };

  VkSubpassDescription subpass_no_msaa = {
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments    = &references[0],
  };

  VkSubpassDependency dependencies[] = {
      {
          .srcSubpass    = VK_SUBPASS_EXTERNAL,
          .dstSubpass    = 0,
          .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      },
      {
          .srcSubpass    = 0,
          .dstSubpass    = VK_SUBPASS_EXTERNAL,
          .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      },
  };

  VkRenderPassCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .subpassCount    = 1,
      .dependencyCount = SDL_arraysize(dependencies),
      .pDependencies   = dependencies,
  };

  if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
  {
    ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
    ci.pAttachments    = attachments_no_msaa;
    ci.pSubpasses      = &subpass_no_msaa;
  }
  else
  {
    ci.attachmentCount = SDL_arraysize(attachments_msaa);
    ci.pAttachments    = attachments_msaa;
    ci.pSubpasses      = &subpass_msaa;
  }

  vkCreateRenderPass(engine.device, &ci, nullptr, &engine.gui_render_pass);
}

} // namespace

void Engine::setup_render_passes()
{
  shadowmap(*this);
  skybox(*this);
  color_and_depth(*this);
  gui(*this);
}
