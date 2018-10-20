#include "engine.hh"

namespace {

void shadowmap(Engine& engine)
{
  for (unsigned i = 0; i < SHADOWMAP_CASCADE_COUNT; ++i)
  {
    VkFramebufferCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = engine.render_passes.shadowmap.render_pass,
        .attachmentCount = 1,
        .pAttachments    = &engine.shadowmap_cascade_image_views[i],
        .width           = SHADOWMAP_IMAGE_DIM,
        .height          = SHADOWMAP_IMAGE_DIM,
        .layers          = 1,
    };

    vkCreateFramebuffer(engine.device, &ci, nullptr, &engine.render_passes.shadowmap.framebuffers[i]);
  }
}

void skybox(Engine& engine)
{
  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments_msaa[]    = {engine.swapchain_image_views[i], engine.msaa_color_image_view};
    VkImageView attachments_no_msaa[] = {engine.swapchain_image_views[i]};

    VkFramebufferCreateInfo ci = {
        .sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = engine.render_passes.skybox.render_pass,
        .width      = engine.extent2D.width,
        .height     = engine.extent2D.height,
        .layers     = 1,
    };

    if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
    {
      ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
      ci.pAttachments    = attachments_no_msaa;
    }
    else
    {
      ci.attachmentCount = SDL_arraysize(attachments_msaa);
      ci.pAttachments    = attachments_msaa;
    }

    vkCreateFramebuffer(engine.device, &ci, nullptr, &engine.render_passes.skybox.framebuffers[i]);
  }
}

void color_and_depth(Engine& engine)
{
  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments_msaa[] = {engine.swapchain_image_views[i], engine.depth_image_view,
                                      engine.msaa_color_image_view};

    VkImageView attachments_no_msaa[] = {engine.swapchain_image_views[i], engine.depth_image_view};

    VkFramebufferCreateInfo ci = {
        .sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = engine.render_passes.color_and_depth.render_pass,
        .width      = engine.extent2D.width,
        .height     = engine.extent2D.height,
        .layers     = 1,
    };

    if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
    {
      ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
      ci.pAttachments    = attachments_no_msaa;
    }
    else
    {
      ci.attachmentCount = SDL_arraysize(attachments_msaa);
      ci.pAttachments    = attachments_msaa;
    }

    vkCreateFramebuffer(engine.device, &ci, nullptr, &engine.render_passes.color_and_depth.framebuffers[i]);
  }
}

void gui(Engine& engine)
{
  for (uint32_t i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
  {
    VkImageView attachments_msaa[]    = {engine.swapchain_image_views[i], engine.msaa_color_image_view};
    VkImageView attachments_no_msaa[] = {engine.swapchain_image_views[i]};

    VkFramebufferCreateInfo ci = {
        .sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = engine.render_passes.gui.render_pass,
        .width      = engine.extent2D.width,
        .height     = engine.extent2D.height,
        .layers     = 1,
    };

    if (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT)
    {
      ci.attachmentCount = SDL_arraysize(attachments_no_msaa);
      ci.pAttachments    = attachments_no_msaa;
    }
    else
    {
      ci.attachmentCount = SDL_arraysize(attachments_msaa);
      ci.pAttachments    = attachments_msaa;
    }

    vkCreateFramebuffer(engine.device, &ci, nullptr, &engine.render_passes.gui.framebuffers[i]);
  }
}

} // namespace

void Engine::setup_framebuffers()
{
  shadowmap(*this);
  skybox(*this);
  color_and_depth(*this);
  gui(*this);
}
