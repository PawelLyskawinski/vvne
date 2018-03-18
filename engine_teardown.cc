#include "engine.hh"
#include <SDL2/SDL_log.h>

void engine_teardown(Engine& engine)
{
  {
    Engine::SimpleRenderer& renderer = engine.simple_renderer;

    for (VkFramebuffer framebuffer : renderer.framebuffers)
      vkDestroyFramebuffer(engine.device, framebuffer, nullptr);

    for (VkPipeline pipeline : renderer.pipelines)
      vkDestroyPipeline(engine.device, pipeline, nullptr);

    for (VkPipelineLayout pipeline_layout : renderer.pipeline_layouts)
      vkDestroyPipelineLayout(engine.device, pipeline_layout, nullptr);

    for (VkDescriptorSetLayout layout : renderer.descriptor_set_layouts)
      vkDestroyDescriptorSetLayout(engine.device, layout, nullptr);

    vkFreeMemory(engine.device, renderer.scene.cube_buffer_memory, nullptr);
    vkDestroyBuffer(engine.device, renderer.scene.cube_buffer, nullptr);

    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      if (VK_NULL_HANDLE != renderer.gui.index_memory[i])
        vkFreeMemory(engine.device, renderer.gui.index_memory[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.index_buffers[i])
        vkDestroyBuffer(engine.device, renderer.gui.index_buffers[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.vertex_memory[i])
        vkFreeMemory(engine.device, renderer.gui.vertex_memory[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.vertex_buffers[i])
        vkDestroyBuffer(engine.device, renderer.gui.vertex_buffers[i], nullptr);
    }

    vkDestroyRenderPass(engine.device, renderer.render_pass, nullptr);

    for (VkFence fence : renderer.submition_fences)
      vkDestroyFence(engine.device, fence, nullptr);
  }

  for (int i = 0; i < engine.loaded_textures; ++i)
  {
    vkFreeMemory(engine.device, engine.images_memory[i], nullptr);
    vkDestroyImage(engine.device, engine.images[i], nullptr);
    vkDestroyImageView(engine.device, engine.image_views[i], nullptr);
  }

  vkDestroyImageView(engine.device, engine.depth_image_view, nullptr);
  vkFreeMemory(engine.device, engine.depth_image_memory, nullptr);
  vkDestroyImage(engine.device, engine.depth_image, nullptr);
  for (VkSampler& sampler : engine.texture_samplers)
    vkDestroySampler(engine.device, sampler, nullptr);
  vkDestroySemaphore(engine.device, engine.image_available, nullptr);
  vkDestroySemaphore(engine.device, engine.render_finished, nullptr);
  vkDestroyCommandPool(engine.device, engine.graphics_command_pool, nullptr);
  vkDestroyDescriptorPool(engine.device, engine.descriptor_pool, nullptr);
  for (VkImageView swapchain_image_view : engine.swapchain_image_views)
    vkDestroyImageView(engine.device, swapchain_image_view, nullptr);
  vkDestroySwapchainKHR(engine.device, engine.swapchain, nullptr);
  vkDestroyDevice(engine.device, nullptr);
  vkDestroySurfaceKHR(engine.instance, engine.surface, nullptr);
  SDL_DestroyWindow(engine.window);

  using Fcn = PFN_vkDestroyDebugReportCallbackEXT;
  auto fcn  = (Fcn)(vkGetInstanceProcAddr(engine.instance, "vkDestroyDebugReportCallbackEXT"));
  fcn(engine.instance, engine.debug_callback, nullptr);
  vkDestroyInstance(engine.instance, nullptr);
}
