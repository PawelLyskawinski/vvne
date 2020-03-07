#include "block_allocator_visualizer.hh"
#include <imgui.h>

namespace {

class Renderer
{
public:
  explicit Renderer(bool start_state)
      : state_fip_flop(start_state)
  {
  }

  void draw(const float length, const char* name)
  {
    if (first_element_rendered)
    {
      ImGui::SameLine(0.0f, 0.0f);
    }
    else
    {
      first_element_rendered = true;
    }

    ImGui::ColorButton(name, state_fip_flop ? ImVec4(1.0f, 0.0f, 0.0f, 0.1f) : ImVec4(0.1f, 0.1f, 0.1f, 0.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(length, 20));

    state_fip_flop = !state_fip_flop;
  }

private:
  bool first_element_rendered = false;
  bool state_fip_flop         = false;
};

} // namespace

void block_allocator_visualize(const BlockAllocator& allocator)
{
  const float    max_width        = ImGui::GetWindowWidth() * 0.98f;
  const uint32_t max_size         = allocator.get_max_size();
  const uint32_t block_capacity   = allocator.get_block_capacity();
  char           name_buffer[128] = {};
  uint64_t       adjacent_count   = 0;

  Renderer renderer(allocator.is_block_used(0));
  for (uint32_t idx = 0; block_capacity > idx; idx += adjacent_count)
  {
    SDL_snprintf(name_buffer, 128, "%s##%u", "block_allocator_visualize", idx);

    adjacent_count                    = allocator.calc_adjacent_blocks_count(idx);
    const float neighbour_blocks_size = static_cast<float>(allocator.get_block_size() * adjacent_count);
    const float length                = max_width * neighbour_blocks_size / static_cast<float>(max_size);

    renderer.draw(length, name_buffer);
  }
}
