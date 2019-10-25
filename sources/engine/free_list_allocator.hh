#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

struct FreeListAllocator
{
  struct Node
  {
    Node*    next;
    unsigned size;

    [[nodiscard]] uint8_t*       as_address() { return reinterpret_cast<uint8_t*>(this); }
    [[nodiscard]] const uint8_t* as_address() const { return reinterpret_cast<const uint8_t*>(this); }
  };

  enum
  {
    FREELIST_ALLOCATOR_CAPACITY_BYTES = 10 * 1024 * 1024
  };

  Node    head;
  uint8_t pool[FREELIST_ALLOCATOR_CAPACITY_BYTES];

  void init();

  template <typename T> T* allocate(uint32_t n = 1) { return reinterpret_cast<T*>(allocate_bytes(sizeof(T) * n)); }
  template <typename T> T* allocate_zeroed(uint32_t n = 1)
  {
    const unsigned requested_size = sizeof(T) * n;
    T*             result         = reinterpret_cast<T*>(allocate_bytes(requested_size));
    SDL_memset(result, 0, requested_size);
    return result;
  }

  template <typename T> void free(T* ptr, uint32_t n = 1)
  {
    free_bytes(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }

private:
  uint8_t* allocate_bytes(unsigned size);
  void     free_bytes(uint8_t* free_me, unsigned size);
};
