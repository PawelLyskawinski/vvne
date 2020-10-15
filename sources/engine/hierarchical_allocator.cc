#include "hierarchical_allocator.hh"
#include "allocators.hh"

HierarchicalAllocator::HierarchicalAllocator()
{
  free_list_5MB.init(5_MB);
  block_allocator_1kb.init(1_KB, 512);
  block_allocator_10kb.init(10_KB, 512);
}

HierarchicalAllocator::~HierarchicalAllocator()
{
  block_allocator_10kb.teardown();
  block_allocator_1kb.teardown();
  free_list_5MB.teardown();
}

void* HierarchicalAllocator::Allocate(uint64_t size)
{
  size = align(size);
  if (1_KB >= size)
  {
    return block_allocator_1kb.allocate();
  }
  else if (10_KB >= size)
  {
    return block_allocator_10kb.allocate();
  }
  else
  {
    return free_list_5MB.allocate_bytes(size);
  }
}

void HierarchicalAllocator::Free(void* ptr, uint64_t size)
{
  size = align(size);
  if (1_KB >= size)
  {
    block_allocator_1kb.free(reinterpret_cast<uint8_t*>(ptr));
  }
  else if (10_KB >= size)
  {
    block_allocator_10kb.free(reinterpret_cast<uint8_t*>(ptr));
  }
  else
  {
    free_list_5MB.free_bytes(reinterpret_cast<uint8_t*>(ptr), size);
  }
}

void* HierarchicalAllocator::Reallocate(void* ptr, uint64_t size)
{
  SDL_assert(false);
  return nullptr;
}
