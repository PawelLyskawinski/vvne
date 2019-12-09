#include "block_allocator_visualizer.hh"
#include <imgui.h>

void block_allocator_visualize(const BlockAllocator& allocator)
{
    const float    max_width = ImGui::GetWindowWidth() * 0.98f;
    const uint32_t max_size  = allocator.get_max_size();
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

    bool state = allocator.is_block_used(0);
    const uint32_t block_capacity = allocator.get_block_capacity();
    uint64_t idx = 0;

    while(block_capacity > idx)
    {
        uint64_t adjacent_count = allocator.calc_adjacent_blocks_count(idx);
        draw_button(state ? BlockType::UsedSpace : BlockType::FreeSpace, allocator.get_block_size() * adjacent_count);
        idx += adjacent_count;
        state = !state;
    }
}
