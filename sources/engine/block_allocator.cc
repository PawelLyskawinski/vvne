#include "block_allocator.hh"
#include <SDL2/SDL_assert.h>
#include <algorithm>

void BlockAllocator::init(uint32_t new_block_size, uint32_t new_block_count)
{
  block_size     = new_block_size;
  block_capacity = new_block_count;
  data           = reinterpret_cast<uint8_t*>(SDL_malloc(new_block_size * new_block_count));
  std::fill(block_usage_bitmaps, &block_usage_bitmaps[SDL_arraysize(block_usage_bitmaps)], uint64_t(0));
}

void BlockAllocator::teardown()
{
  SDL_free(data);
}

namespace {

inline bool is_bit_set(uint64_t bitmap, uint64_t bit)
{
  return bitmap & (uint64_t(1) << bit);
}

inline void set_bit(uint64_t& bitmap, uint64_t bit)
{
  bitmap |= (uint64_t(1) << bit);
}

inline void clear_bit(uint64_t& bitmap, uint64_t bit)
{
  bitmap &= ~(uint64_t(1) << bit);
}

inline bool is_between(const uint8_t* min, const uint8_t* test, const uint8_t* max)
{
  return (test >= min) && (test < max);
}

} // namespace

uint8_t* BlockAllocator::allocate()
{
  for (uint32_t i = 0; i < block_capacity; ++i)
  {
    uint64_t&      bitmap = block_usage_bitmaps[i / 64];
    const uint64_t offset = i % 64;

    if (!is_bit_set(bitmap, offset))
    {
      set_bit(bitmap, offset);
      return &data[block_size * i];
    }
  }
  SDL_assert(false);
  return nullptr;
}

void BlockAllocator::free(const uint8_t* ptr)
{
  // freed pointer has to be inside internally managed memory
  SDL_assert(is_between(data, ptr, &data[block_size * block_capacity]));

  const uint64_t memory_offset = (ptr - data);
  const uint64_t block_idx     = memory_offset / block_size;
  uint64_t&      bitmap        = block_usage_bitmaps[block_idx / 64];
  const uint64_t offset        = block_idx % 64;

  // attempting to free already freed memory?
  SDL_assert(is_bit_set(bitmap, offset));

  clear_bit(bitmap, offset);
}

bool BlockAllocator::is_block_used(uint64_t idx) const
{
    SDL_assert(block_capacity > idx);
    return is_bit_set(block_usage_bitmaps[idx / 64], idx % 64);
}

uint64_t BlockAllocator::calc_adjacent_blocks_count(uint64_t first) const
{
  bool initial_state = is_bit_set(block_usage_bitmaps[first / 64], first % 64);

  uint64_t it = first;
  while ((block_capacity > it) and (initial_state == is_block_used(it)))
  {
    ++it;
  }

  return it - first;
}

uint64_t BlockAllocator::get_max_size() const
{
  return block_size * block_capacity;
}
