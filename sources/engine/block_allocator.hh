#pragma once

#include "memory_allocator.hh"

//
// Fixed size block allocator.
// Internal bitmap allows maximum of 1280 blocks, but will be capped depending on the available memory and block size.
//
// Allows to ONLY allocate a single block
//
struct BlockAllocator : public MemoryAllocator
{
  BlockAllocator(uint32_t block_size, uint32_t block_count);
  ~BlockAllocator() override;

  void* Allocate(uint64_t size) override;
  void* Reallocate(void* ptr, uint64_t size) override;
  void  Free(void* ptr, uint64_t size) override;

  uint8_t* m_Data                  = nullptr;
  uint64_t m_BlockUsageBitmaps[20] = {};
  uint32_t m_BlockSize             = 0;
  uint32_t m_BlockCapacity         = 0;
};
