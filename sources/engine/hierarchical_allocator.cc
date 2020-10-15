#include "hierarchical_allocator.hh"
#include "allocators.hh"

void HierarchicalAllocator::init()
{
  free_list_5MB.init(5_MB);
  block_allocator_1kb.init(1_KB, 512);
  block_allocator_10kb.init(10_KB, 512);
}

void HierarchicalAllocator::teardown()
{
  block_allocator_10kb.teardown();
  block_allocator_1kb.teardown();
  free_list_5MB.teardown();
}

uint8_t* HierarchicalAllocator::allocate_bytes(unsigned size)
{
  size = align(size, 16u);
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

void HierarchicalAllocator::free_bytes(uint8_t* free_me, unsigned size)
{
  size = align(size, 16u);
  if (1_KB >= size)
  {
    block_allocator_1kb.free(free_me);
  }
  else if (10_KB >= size)
  {
    block_allocator_10kb.free(free_me);
  }
  else
  {
    free_list_5MB.free_bytes(free_me, size);
  }
}
