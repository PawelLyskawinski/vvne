#pragma once

#include "block_allocator.hh"
#include "free_list_allocator.hh"
#include "memory_allocator.hh"

struct HierarchicalAllocator : public MemoryAllocator
{
  HierarchicalAllocator();
  ~HierarchicalAllocator() override = default;

  template <typename T> T* allocate(uint64_t n = 1)
  {
    return reinterpret_cast<T*>(Allocate(sizeof(T) * n));
  }

  template <typename T> T* allocate_zeroed(uint64_t n = 1)
  {
    const unsigned requested_size = sizeof(T) * n;
    T*             result         = reinterpret_cast<T*>(Allocate(requested_size));
    SDL_memset(result, 0, requested_size);
    return result;
  }

  template <typename T> void free(T* ptr, uint32_t n = 1)
  {
    Free(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }

  void* Allocate(uint64_t size) override;
  void  Free(void* ptr, uint64_t size) override;
  void* Reallocate(void* ptr, uint64_t size) override;

  BlockAllocator    block_allocator_1kb;
  BlockAllocator    block_allocator_10kb;
  FreeListAllocator free_list_5MB;
};
