#pragma once

#include "free_list_allocator.hh"

//
// Renders the allocation visualization bar in current imgui window
// Red   - used memory
// Black - free memory
//
void free_list_visualize(const FreeListAllocator& allocator);
