#include "gpu_memory_visualizer.hh"
#include "engine.hh"
#include <imgui.h>

void gpu_memory_visualize(const GpuMemoryAllocator& allocator)
{
  const float    max_width = ImGui::GetWindowWidth() * 0.98f;
  const uint32_t max_size  = allocator.max_size;
  uint32_t       counter   = 0;
  char           name_buffer[128];

  enum class BlockType
  {
    FreeSpace,
    UsedSpace
  };

  auto draw_button = [&name_buffer, &counter, max_size, max_width](BlockType type, VkDeviceSize size) {
    if (counter)
    {
      ImGui::SameLine(0.0f, 0.0f);
    }
    const float length = max_width * (static_cast<float>(size) / static_cast<float>(max_size));
    // TODO This function can be used only once thanks to naming collisions.. fix it!
    SDL_snprintf(name_buffer, 128, "%s##%u", "gpu_memory_visualize", counter++);
    ImGui::ColorButton(name_buffer,
                       (BlockType::FreeSpace == type) ? ImVec4(0.1f, 0.1f, 0.1f, 0.0f) : ImVec4(1.0f, 0.0f, 0.0f, 0.1f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(length, 20));
  };

  if(allocator.nodes[0].offset > 0)
  {
      draw_button(BlockType::UsedSpace, allocator.nodes[0].offset);
  }

  for(uint32_t i=0; i<allocator.nodes_count; ++i)
  {
      draw_button(BlockType::FreeSpace, allocator.nodes[i].size);
      if(i < (allocator.nodes_count - 1))
      {
          draw_button(BlockType::UsedSpace, allocator.nodes[i+1].offset - (allocator.nodes[i].offset + allocator.nodes[i].size));
      }
  }
}
