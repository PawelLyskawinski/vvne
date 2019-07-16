#include "engine.hh"

namespace {

void shadowmap(Engine& engine)
{
  RenderPass& render_pass = engine.render_passes.shadowmap;

  VkFramebufferCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = render_pass.render_pass,
      .attachmentCount = 1,
      .width           = SHADOWMAP_IMAGE_DIM,
      .height          = SHADOWMAP_IMAGE_DIM,
      .layers          = 1,
  };

  for (unsigned i = 0; i < render_pass.framebuffers_count; ++i)
  {
    ci.pAttachments = &engine.shadowmap_cascade_image_views[i];
    vkCreateFramebuffer(engine.device, &ci, nullptr, &render_pass.framebuffers[i]);
  }
}

void skybox(Engine& engine)
{
  RenderPass& render_pass = engine.render_passes.skybox;

  VkImageView attachments[] = {VK_NULL_HANDLE, engine.msaa_color_image.image_view};

  VkFramebufferCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = render_pass.render_pass,
      .attachmentCount = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 2u : 1u,
      .pAttachments    = attachments,
      .width           = engine.extent2D.width,
      .height          = engine.extent2D.height,
      .layers          = 1,
  };

  for (uint32_t i = 0; i < render_pass.framebuffers_count; ++i)
  {
    attachments[0] = engine.swapchain_image_views[i];
    vkCreateFramebuffer(engine.device, &ci, nullptr, &render_pass.framebuffers[i]);
  }
}

void color_and_depth(Engine& engine)
{
  RenderPass& render_pass = engine.render_passes.color_and_depth;

  VkImageView attachments[] = {VK_NULL_HANDLE, engine.depth_image.image_view, engine.msaa_color_image.image_view};

  VkFramebufferCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = render_pass.render_pass,
      .attachmentCount = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 2u : 3u,
      .pAttachments    = attachments,
      .width           = engine.extent2D.width,
      .height          = engine.extent2D.height,
      .layers          = 1,
  };

  for (uint32_t i = 0; i < render_pass.framebuffers_count; ++i)
  {
    attachments[0] = engine.swapchain_image_views[i];
    vkCreateFramebuffer(engine.device, &ci, nullptr, &render_pass.framebuffers[i]);
  }
}

void gui(Engine& engine)
{
  RenderPass& render_pass = engine.render_passes.gui;

  VkImageView attachments[] = {VK_NULL_HANDLE, engine.msaa_color_image.image_view};

  VkFramebufferCreateInfo ci = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass      = render_pass.render_pass,
      .attachmentCount = (VK_SAMPLE_COUNT_1_BIT == engine.MSAA_SAMPLE_COUNT) ? 1u : 2u,
      .pAttachments    = attachments,
      .width           = engine.extent2D.width,
      .height          = engine.extent2D.height,
      .layers          = 1,
  };

  for (uint32_t i = 0; i < render_pass.framebuffers_count; ++i)
  {
    attachments[0] = engine.swapchain_image_views[i];
    vkCreateFramebuffer(engine.device, &ci, nullptr, &render_pass.framebuffers[i]);
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
