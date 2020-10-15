#include "block_allocator.hh"
#include <SDL2/SDL_assert.h>

namespace {

inline bool is_bit_set(uint64_t bitmap, uint64_t bit)
{
  return bitmap & (uint64_t(1) << bit);
}

} // namespace

BlockAllocator::BlockAllocator(uint32_t new_block_size, uint32_t new_block_count)
    : m_Data(reinterpret_cast<uint8_t*>(SDL_malloc(new_block_size * new_block_count)))
    , m_BlockUsageBitmaps{}
    , m_BlockSize(new_block_size)
    , m_BlockCapacity(new_block_count)
{
}

BlockAllocator::~BlockAllocator()
{
  SDL_free(m_Data);
}

void* BlockAllocator::Allocate(uint64_t size)
{
  SDL_assert(size <= m_BlockSize);

  auto set_bit = [](uint64_t& bitmap, uint64_t bit) { bitmap |= (uint64_t(1) << bit); };
  for (uint32_t i = 0; i < m_BlockCapacity; ++i)
  {
    uint64_t&      bitmap = m_BlockUsageBitmaps[i / 64];
    const uint64_t offset = i % 64;

    if (!is_bit_set(bitmap, offset))
    {
      set_bit(bitmap, offset);
      return &m_Data[m_BlockSize * i];
    }
  }

  SDL_assert(false);
  return nullptr;
}

void* BlockAllocator::Reallocate(void* ptr, uint64_t size)
{
  SDL_assert(size <= m_BlockSize);
  return ptr;
}

void BlockAllocator::Free(void* ptr, uint64_t size)
{
  //
  // freed pointer has to be inside internally managed memory
  //
  [[maybe_unused]] auto is_between = [](const uint8_t* min, const uint8_t* test, const uint8_t* max) {
    return (test >= min) && (test < max);
  };
  SDL_assert(is_between(m_Data, reinterpret_cast<uint8_t*>(ptr), &m_Data[m_BlockSize * m_BlockCapacity]));

  const uint64_t memory_offset = (reinterpret_cast<uint8_t*>(ptr) - m_Data);
  const uint64_t block_idx     = memory_offset / m_BlockSize;
  uint64_t&      bitmap        = m_BlockUsageBitmaps[block_idx / 64];
  const uint64_t offset        = block_idx % 64;

  //
  // attempting to free already freed memory?
  //
  SDL_assert(is_bit_set(bitmap, offset));

  auto clear_bit = [](uint64_t& bitmap, uint64_t bit) { bitmap &= ~(uint64_t(1) << bit); };
  clear_bit(bitmap, offset);
}
