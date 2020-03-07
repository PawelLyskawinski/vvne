#include "block_allocator.hh"
#include <SDL2/SDL_assert.h>
#include <algorithm>

void BlockAllocator::init(uint32_t new_block_size, uint32_t new_block_count)
{
  block_size     = new_block_size;
  block_capacity = new_block_count;
  data           = reinterpret_cast<uint8_t*>(SDL_malloc(new_block_size * new_block_count));
  block_usage_bitmap.clear();
}

void BlockAllocator::teardown()
{
  SDL_free(data);
}

uint8_t* BlockAllocator::allocate()
{
  for (uint32_t i = 0; i < block_capacity; ++i)
  {
    if (not block_usage_bitmap.test(i))
    {
      block_usage_bitmap.set(i);
      return &data[block_size * i];
    }
  }
  SDL_assert(false);
  return nullptr;
}

void BlockAllocator::free(const uint8_t* ptr)
{
  SDL_assert(data <= ptr);
  SDL_assert(ptr < &data[block_size * block_capacity]);

  const uint64_t memory_offset = (ptr - data);
  const uint64_t block_idx     = memory_offset / block_size;

  SDL_assert(block_usage_bitmap.test(block_idx));
  block_usage_bitmap.clear(block_idx);
}

bool BlockAllocator::is_block_used(uint64_t idx) const
{
  SDL_assert(block_capacity > idx);
  return block_usage_bitmap.test(idx);
}

uint64_t BlockAllocator::calc_adjacent_blocks_count(uint64_t first) const
{
  const bool initial_state = block_usage_bitmap.test(first);
  uint64_t   it            = first;
  while ((block_capacity > it) and (initial_state == block_usage_bitmap.test(it)))
  {
    ++it;
  }
  return it - first;
}

uint64_t BlockAllocator::get_max_size() const
{
  return block_size * block_capacity;
}
