#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

struct FreeListAllocator
{
  struct Node
  {
    Node*    next;
    unsigned size;
  };

  enum
  {
    FREELIST_ALLOCATOR_CAPACITY_BYTES = 10000
  };

  uint8_t pool[FREELIST_ALLOCATOR_CAPACITY_BYTES];
  Node    head;

  void     init();
  uint8_t* allocate_bytes(unsigned size);
  void     free_bytes(uint8_t* free_me, unsigned count);

  template <typename T> T*   allocate(uint32_t n = 1) { return reinterpret_cast<T*>(allocate_bytes(sizeof(T) * n)); }
  template <typename T> void free(T* ptr, uint32_t n = 1)
  {
    free_bytes(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }
};
