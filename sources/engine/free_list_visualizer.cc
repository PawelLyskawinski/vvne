#include "free_list_visualizer.hh"
#include "imgui.h"

static float calc_proportion(const uint8_t* begin, const uint8_t* iter, uint32_t max)
{
  const uint64_t distance = iter - begin;
  return static_cast<float>(distance) / static_cast<float>(max);
}

static float calc_proportion(uint32_t size, uint32_t max) { return static_cast<float>(size) / static_cast<float>(max); }

void free_list_visualize(const FreeListAllocator& allocator)
{
  //
  // ALGORITHM OVERVIEW
  //
  // |----------------------------------------|
  // | USED | FREE | USED | FREE              |
  // |----------------------------------------|
  //
  // We'll want final result to end up like a picture above.
  // Whole point of the algorithm below will be to traverse the single linked list and draw the spaces with correct
  // width. As a data structure the whole thing looks like this:
  //
  // Head ---------O             O------------------- NULL
  //      |----------------------------------------|
  //      | USED | FREE | USED | FREE              |
  //      |----------------------------------------|
  //               O-------------O
  //
  // Few things to note here:
  // - We iterate between each free list node which knows about its POSITION and LENGTH
  // - We have immediate access through it to the next node (so also knowing the POSITION and LENGTH of next iteration)
  // - LENGTH of the whole data structure is known from the very beginning
  // - Head node is an exception: it only knows about position of the first node
  //
  // Given that we can form a pseudo-code procedure which will let us draw this thing:
  // 1. Initialize iterator A with head->next
  // 2. Handle the initial special case:
  //    2.1 IF head node does not point to any next element
  //        2.1.1 The allocator is full! -> ALGORITHM FINISH!
  //    2.2 IF data structure does not start with free list node:
  //        2.2.1 Draw used space as the first portion, then draw the A
  // 3. LOOP
  //    3.1 Check if the node has a next item
  //        3.1.1 TRUE  -> Draw the used space between A and A->next and also A->next
  //        3.1.2 FALSE ->  ALGORITHM FINISH! (but do the things below before finalizing)
  //            3.1.2.1 IF current node does not fill the space to the end of data structure
  //                3.1.2.1.1 Draw the remainder space

  const float    max_width = ImGui::GetWindowWidth() * 0.98f;
  const uint32_t max_size  = allocator.FREELIST_ALLOCATOR_CAPACITY_BYTES;
  uint32_t       counter   = 0;
  char           name_buffer[128];

  enum class BlockType
  {
    FreeSpace,
    UsedSpace
  };

  auto draw_button = [&name_buffer, &counter](BlockType type, float length) {
    if (counter)
    {
      ImGui::SameLine(0.0f, 0.0f);
    }
    // TODO This function can be used only once thanks to naming collisions.. fix it!
    SDL_snprintf(name_buffer, 128, "%s##%u", "free_list_visualize", counter++);
    ImGui::ColorButton(name_buffer,
                       (BlockType::FreeSpace == type) ? ImVec4(0.1f, 0.1f, 0.1f, 0.0f) : ImVec4(1.0f, 0.0f, 0.0f, 0.1f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(length, 20));
  };

  const FreeListAllocator::Node* current = allocator.head.next;
  if (nullptr == current)
  {
    draw_button(BlockType::UsedSpace, max_width);
  }
  else
  {
    const uint8_t* current_ptr = reinterpret_cast<const uint8_t*>(current);
    if (current_ptr != allocator.pool)
    {
      draw_button(BlockType::UsedSpace, max_width * calc_proportion(allocator.pool, current_ptr, max_size));
    }
    draw_button(BlockType::FreeSpace, max_width * calc_proportion(current->size, max_size));

    while (current)
    {
      if (current->next)
      {
        const uint8_t* next_ptr = reinterpret_cast<const uint8_t*>(current->next);
        draw_button(BlockType::UsedSpace, max_width * calc_proportion(current_ptr + current->size, next_ptr, max_size));
        draw_button(BlockType::FreeSpace,
                    max_width * calc_proportion(next_ptr, next_ptr + current->next->size, max_size));
      }
      else
      {
        if ((current_ptr + current->size) != &allocator.pool[max_size])
        {
          draw_button(BlockType::UsedSpace,
                      max_width * calc_proportion(current_ptr + current->size, &allocator.pool[max_size], max_size));
        }
      }
      current     = current->next;
      current_ptr = reinterpret_cast<const uint8_t*>(current);
    }
  }
}
