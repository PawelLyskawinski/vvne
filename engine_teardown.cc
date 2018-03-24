#include "engine.hh"

void Engine::teardown()
{
  {
    Engine::SimpleRenderer& renderer = simple_renderer;

    for (VkFramebuffer framebuffer : renderer.framebuffers)
      vkDestroyFramebuffer(device, framebuffer, nullptr);

    for (VkPipeline pipeline : renderer.pipelines)
      vkDestroyPipeline(device, pipeline, nullptr);

    for (VkPipelineLayout pipeline_layout : renderer.pipeline_layouts)
      vkDestroyPipelineLayout(device, pipeline_layout, nullptr);

    for (VkDescriptorSetLayout layout : renderer.descriptor_set_layouts)
      vkDestroyDescriptorSetLayout(device, layout, nullptr);

    vkFreeMemory(device, renderer.scene.cube_buffer_memory, nullptr);
    vkDestroyBuffer(device, renderer.scene.cube_buffer, nullptr);

    for (int i = 0; i < SWAPCHAIN_IMAGES_COUNT; ++i)
    {
      if (VK_NULL_HANDLE != renderer.gui.index_memory[i])
        vkFreeMemory(device, renderer.gui.index_memory[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.index_buffers[i])
        vkDestroyBuffer(device, renderer.gui.index_buffers[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.vertex_memory[i])
        vkFreeMemory(device, renderer.gui.vertex_memory[i], nullptr);

      if (VK_NULL_HANDLE != renderer.gui.vertex_buffers[i])
        vkDestroyBuffer(device, renderer.gui.vertex_buffers[i], nullptr);
    }

    vkDestroyRenderPass(device, renderer.render_pass, nullptr);

    for (VkFence fence : renderer.submition_fences)
      vkDestroyFence(device, fence, nullptr);
  }

  for (int i = 0; i < loaded_textures; ++i)
  {
    vkFreeMemory(device, images_memory[i], nullptr);
    vkDestroyImage(device, images[i], nullptr);
    vkDestroyImageView(device, image_views[i], nullptr);
  }

  vkDestroyImageView(device, depth_image_view, nullptr);
  vkFreeMemory(device, depth_image_memory, nullptr);
  vkDestroyImage(device, depth_image, nullptr);
  for (VkSampler& sampler : texture_samplers)
    vkDestroySampler(device, sampler, nullptr);
  vkDestroySemaphore(device, image_available, nullptr);
  vkDestroySemaphore(device, render_finished, nullptr);
  vkDestroyCommandPool(device, graphics_command_pool, nullptr);
  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
  for (VkImageView swapchain_image_view : swapchain_image_views)
    vkDestroyImageView(device, swapchain_image_view, nullptr);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  SDL_DestroyWindow(window);

  using Fcn = PFN_vkDestroyDebugReportCallbackEXT;
  auto fcn  = (Fcn)(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
  fcn(instance, debug_callback, nullptr);
  vkDestroyInstance(instance, nullptr);
}
