#pragma once

#include "gpu_memory_allocator.hh"

//
// Renders the allocation visualization bar in current imgui window
// Red   - used memory
// Black - free memory
//
void gpu_memory_visualize(const GpuMemoryAllocator& allocator);
