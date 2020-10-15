#include "block_allocator_visualizer.hh"
#include <SDL2/SDL_assert.h>
#include <imgui.h>

namespace {

uint64_t get_max_size(const BlockAllocator& allocator)
{
  return allocator.m_BlockSize * allocator.m_BlockCapacity;
}

bool is_bit_set(uint64_t bitmap, uint64_t bit)
{
  return bitmap & (uint64_t(1) << bit);
}

bool is_block_used(const BlockAllocator& allocator, uint64_t idx)
{
  SDL_assert(allocator.m_BlockCapacity > idx);
  auto is_bit_set = [](uint64_t bitmap, uint64_t bit) { return bitmap & (uint64_t(1) << bit); };
  return is_bit_set(allocator.m_BlockUsageBitmaps[idx / 64], idx % 64);
}

uint64_t calc_adjacent_blocks_count(const BlockAllocator& allocator, uint64_t first)
{
  bool initial_state = is_bit_set(allocator.m_BlockUsageBitmaps[first / 64], first % 64);

  uint64_t it = first;
  while ((allocator.m_BlockCapacity > it) and (initial_state == is_block_used(allocator, it)))
  {
    ++it;
  }

  return it - first;
}

} // namespace

void block_allocator_visualize(const BlockAllocator& allocator)
{
  const float    max_width = ImGui::GetWindowWidth() * 0.98f;
  const uint32_t max_size  = get_max_size(allocator);
  uint32_t       counter   = 0;
  char           name_buffer[128];

  enum class BlockType
  {
    FreeSpace,
    UsedSpace
  };

  auto draw_button = [&name_buffer, &counter, max_size, max_width](BlockType type, uint64_t size) {
    if (counter)
    {
      ImGui::SameLine(0.0f, 0.0f);
    }
    const float length = max_width * (static_cast<float>(size) / static_cast<float>(max_size));
    // TODO This function can be used only once thanks to naming collisions.. fix it!
    SDL_snprintf(name_buffer, 128, "%s##%u", "block_allocator_visualize", counter++);
    ImGui::ColorButton(name_buffer,
                       (BlockType::FreeSpace == type) ? ImVec4(0.1f, 0.1f, 0.1f, 0.0f) : ImVec4(1.0f, 0.0f, 0.0f, 0.1f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(length, 20));
  };

  // @TODO for each adjacent used/free strides draw single colored button

  bool     state = is_block_used(allocator, 0);
  uint64_t idx   = 0;

  while (allocator.m_BlockCapacity > idx)
  {
    uint64_t adjacent_count = calc_adjacent_blocks_count(allocator, idx);
    draw_button(state ? BlockType::UsedSpace : BlockType::FreeSpace, allocator.m_BlockSize * adjacent_count);
    idx += adjacent_count;
    state = !state;
  }
}
