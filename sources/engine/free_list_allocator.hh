#pragma once

#include "vtl/literals.hh"
#include <SDL2/SDL_assert.h>

struct FreeListAllocator
{
  struct Node
  {
    Node*    next;
    unsigned size;

    [[nodiscard]] uint8_t* as_address()
    {
      return reinterpret_cast<uint8_t*>(this);
    }

    [[nodiscard]] const uint8_t* as_address() const
    {
      return reinterpret_cast<const uint8_t*>(this);
    }
  };

  Node     head;
  uint8_t* pool;
  uint64_t capacity;

  void     init(uint64_t new_capacity);
  void     teardown();
  uint8_t* allocate_bytes(unsigned size);
  void     free_bytes(uint8_t* free_me, unsigned size);
};
