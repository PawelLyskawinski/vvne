#pragma once

#include "block_allocator.hh"

//
// Renders the allocation visualization bar in current imgui window
// Red   - used memory
// Black - free memory
//
void block_allocator_visualize(const BlockAllocator& allocator);
