#include "hierarchical_allocator.hh"
#include "allocators.hh"

HierarchicalAllocator::HierarchicalAllocator()
    : block_allocator_1kb(1_KB, 512)
    , block_allocator_10kb(10_KB, 512)
{
  free_list_5MB.init(5_MB);
}

HierarchicalAllocator::~HierarchicalAllocator()
{
  free_list_5MB.teardown();
}

void* HierarchicalAllocator::Allocate(uint64_t size)
{
  size = align(size);
  if (1_KB >= size)
  {
    return block_allocator_1kb.Allocate(size);
  }
  else if (10_KB >= size)
  {
    return block_allocator_10kb.Allocate(size);
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
    block_allocator_1kb.Free(ptr, size);
  }
  else if (10_KB >= size)
  {
    block_allocator_10kb.Free(ptr, size);
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
