#pragma once

#include "memory_allocator.hh"

struct FreeListAllocator : public MemoryAllocator
{
  struct Node
  {
    Node*    next;
    unsigned size;
  };

  explicit FreeListAllocator(uint64_t capacity);
  ~FreeListAllocator() override;

  void* Allocate(uint64_t size) override;
  void* Reallocate(void* ptr, uint64_t size) override;
  void  Free(void* ptr, uint64_t size) override;

  Node     head;
  uint8_t* pool;
  uint64_t capacity;
};
