#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_log.h>
#include "engine.hh"
#include "game.hh"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void game_startup(Game& game, Engine& engine)
{
  game.clouds_texture_idx       = engine_load_texture(engine, "../assets/clouds.png");
  game.clouds_bliss_texture_idx = engine_load_texture(engine, "../assets/clouds_bliss_blue.jpg");

  {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    unsigned char* guifont_pixels = nullptr;
    int            guifont_w      = 0;
    int            guifont_h      = 0;
    io.Fonts->GetTexDataAsRGBA32(&guifont_pixels, &guifont_w, &guifont_h);
    SDL_Surface* surface  = SDL_CreateRGBSurfaceWithFormatFrom(guifont_pixels, guifont_w, guifont_h, 32, 4 * guifont_w,
                                                              SDL_PIXELFORMAT_RGBA8888);
    game.font_texture_idx = engine_load_texture(engine, surface);
    SDL_FreeSurface(surface);

    struct Mapping
    {
      ImGuiKey_    imgui;
      SDL_Scancode sdl;
    } mappings[] = {{ImGuiKey_Tab, SDL_SCANCODE_TAB},
                    {ImGuiKey_LeftArrow, SDL_SCANCODE_LEFT},
                    {ImGuiKey_RightArrow, SDL_SCANCODE_RIGHT},
                    {ImGuiKey_UpArrow, SDL_SCANCODE_UP},
                    {ImGuiKey_DownArrow, SDL_SCANCODE_DOWN},
                    {ImGuiKey_PageUp, SDL_SCANCODE_PAGEUP},
                    {ImGuiKey_PageDown, SDL_SCANCODE_PAGEDOWN},
                    {ImGuiKey_Home, SDL_SCANCODE_HOME},
                    {ImGuiKey_End, SDL_SCANCODE_END},
                    {ImGuiKey_Insert, SDL_SCANCODE_INSERT},
                    {ImGuiKey_Delete, SDL_SCANCODE_DELETE},
                    {ImGuiKey_Backspace, SDL_SCANCODE_BACKSPACE},
                    {ImGuiKey_Space, SDL_SCANCODE_SPACE},
                    {ImGuiKey_Enter, SDL_SCANCODE_RETURN},
                    {ImGuiKey_Escape, SDL_SCANCODE_ESCAPE},
                    {ImGuiKey_A, SDL_SCANCODE_A},
                    {ImGuiKey_C, SDL_SCANCODE_C},
                    {ImGuiKey_V, SDL_SCANCODE_V},
                    {ImGuiKey_X, SDL_SCANCODE_X},
                    {ImGuiKey_Y, SDL_SCANCODE_Y},
                    {ImGuiKey_Z, SDL_SCANCODE_Z}};

    for (Mapping mapping : mappings)
      io.KeyMap[mapping.imgui] = mapping.sdl;

    io.RenderDrawListsFn  = nullptr;
    io.GetClipboardTextFn = [](void*) -> const char* { return SDL_GetClipboardText(); };
    io.SetClipboardTextFn = [](void*, const char* text) { SDL_SetClipboardText(text); };
    io.ClipboardUserData  = nullptr;

    struct CursorMapping
    {
      ImGuiMouseCursor_ imgui;
      SDL_SystemCursor  sdl;
    } cursor_mappings[] = {{ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW},
                           {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM},
                           {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL},
                           {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS},
                           {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE},
                           {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW},
                           {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE}};

    for (CursorMapping mapping : cursor_mappings)
      game.mousecursors[mapping.imgui] = SDL_CreateSystemCursor(mapping.sdl);
  }

  {
    const int memorySize = 1600; // adjusted manually
    game.helmet.memory   = static_cast<uint8_t*>(SDL_malloc(memorySize));
    game.helmet.loadASCII("../assets/DamagedHelmet/glTF/DamagedHelmet.gltf");
    SDL_Log("helmet used %d / %d bytes", game.helmet.usedMemory, memorySize);
    game.helmet.debugDump();
    game.renderableHelmet.construct(engine, game.helmet);
  }

  // ----------------------------------------------------------
  //                   UPDATE DESCRIPTOR SET
  // ----------------------------------------------------------

  for (int image_index = 0; image_index < SWAPCHAIN_IMAGES_COUNT; ++image_index)
  {
    Engine::SimpleRenderer& renderer = engine.simple_renderer;

    // texture gui font
    {
      VkDescriptorImageInfo image{};
      image.sampler     = engine.texture_samplers[image_index];
      image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.imageView   = engine.image_views[game.font_texture_idx];

      VkWriteDescriptorSet write{};
      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet          = renderer.descriptor_sets[(image_index * 4) + game.font_texture_idx];
      write.dstBinding      = 1;
      write.dstArrayElement = 0;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo      = &image;

      vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
    }

    // texture clouds bliss
    {
      VkDescriptorImageInfo image{};
      image.sampler     = engine.texture_samplers[image_index];
      image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.imageView   = engine.image_views[game.clouds_texture_idx];

      VkWriteDescriptorSet write{};
      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet          = renderer.descriptor_sets[(image_index * 4) + game.clouds_texture_idx];
      write.dstBinding      = 1;
      write.dstArrayElement = 0;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo      = &image;

      vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
    }
    // gui font
    {
      VkDescriptorImageInfo image{};
      image.sampler     = engine.texture_samplers[image_index];
      image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.imageView   = engine.image_views[game.clouds_bliss_texture_idx];

      VkWriteDescriptorSet write{};
      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet          = renderer.descriptor_sets[(image_index * 4) + game.clouds_bliss_texture_idx];
      write.dstBinding      = 1;
      write.dstArrayElement = 0;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo      = &image;

      vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
    }

    {
      VkDescriptorImageInfo image{};
      image.sampler     = engine.texture_samplers[image_index];
      image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.imageView   = engine.image_views[game.renderableHelmet.albedo_texture_idx];

      VkWriteDescriptorSet write{};
      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet          = renderer.descriptor_sets[(image_index * 4) + game.renderableHelmet.albedo_texture_idx];
      write.dstBinding      = 1;
      write.dstArrayElement = 0;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo      = &image;

      vkUpdateDescriptorSets(engine.device, 1, &write, 0, nullptr);
    }
  }

  game.helmet_translation[0] = 2.2f;
  game.helmet_translation[1] = 3.5f;
  game.helmet_translation[2] = 19.2f;
}
