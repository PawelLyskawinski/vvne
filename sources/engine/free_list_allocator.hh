#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

class FreeListAllocator
{
public:
  static constexpr uint32_t Capacity = 10 * 1024 * 1024;

  FreeListAllocator();

  template <typename T> T*   allocate(uint32_t n = 1) { return reinterpret_cast<T*>(allocate_bytes(sizeof(T) * n)); }
  template <typename T> void free(T* ptr, uint32_t n = 1)
  {
    free_bytes(reinterpret_cast<uint8_t*>(ptr), sizeof(T) * n);
  }

  struct Node
  {
    Node*    next;
    unsigned size;
  };

private:
  uint8_t* allocate_bytes(unsigned size);
  void     free_bytes(uint8_t* free_me, unsigned size);

private:
  Node    head;
  uint8_t pool[Capacity]{};
};
