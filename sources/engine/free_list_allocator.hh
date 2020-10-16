#pragma once

#include "memory_allocator.hh"

struct FreeListAllocator : public MemoryAllocator
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

  explicit FreeListAllocator(uint64_t capacity);
  ~FreeListAllocator() override;

  void* Allocate(uint64_t size) override;
  void* Reallocate(void* ptr, uint64_t size) override;
  void  Free(void* ptr, uint64_t size) override;

  Node     head;
  uint8_t* pool;
  uint64_t capacity;

#if 0
  void     init(uint64_t new_capacity);
  void     teardown();
  uint8_t* allocate_bytes(unsigned size);
  void     free_bytes(uint8_t* free_me, unsigned size);
#endif
};
