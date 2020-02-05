#pragma once

#include "block_allocator.hh"
#include "free_list_allocator.hh"
#include <SDL2/SDL_mutex.h>

struct HierarchicalAllocator
{
  BlockAllocator    block_allocator_1kb;
  BlockAllocator    block_allocator_10kb;
  FreeListAllocator free_list_5MB;

  void init();
  void teardown();

  template <typename T> T* allocate(uint32_t n = 1)
  {
    return reinterpret_cast<T*>(allocate_bytes(sizeof(T) * n));
  }

  template <typename T> T* allocate_threadsafe(uint32_t n = 1)
  {
    return reinterpret_cast<T*>(allocate_bytes_threadsafe(sizeof(T) * n));
  }

  template <typename T> T* allocate_zeroed(uint32_t n = 1)
  {
    const unsigned requested_size = sizeof(T) * n;
    T*             result         = reinterpret_cast<T*>(allocate_bytes(requested_size));
    SDL_memset(result, 0, requested_size);
    return result;
  }

  template <typename T> T* allocate_zeroed_threadsafe(uint32_t n = 1)
  {
    const unsigned requested_size = sizeof(T) * n;
    T*             result         = reinterpret_cast<T*>(allocate_bytes_threadsafe(requested_size));
    SDL_memset(result, 0, requested_size);
    return result;
  }

  template <typename T> void free(T* ptr, uint32_t n = 1)
  {
    free_bytes(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }

  template <typename T> void free_threadsafe(T* ptr, uint32_t n = 1)
  {
    free_bytes_threadsafe(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }

private:
  uint8_t*   allocate_bytes(unsigned size);
  uint8_t*   allocate_bytes_threadsafe(unsigned size);
  void       free_bytes(uint8_t* free_me, unsigned size);
  void       free_bytes_threadsafe(uint8_t* free_me, unsigned size);
  SDL_mutex* access_mutex;
};
