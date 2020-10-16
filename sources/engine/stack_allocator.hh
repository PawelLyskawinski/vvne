#pragma once

#include "memory_allocator.hh"

class Stack : public MemoryAllocator
{
public:
  explicit Stack(uint64_t new_capacity);
  ~Stack() override;

  void* Allocate(uint64_t size) override;
  void* Reallocate(void* ptr, uint64_t size) override;
  void  Free(void* ptr, uint64_t size) override;
  void  Reset();

private:
  uint8_t* data;
  uint64_t sp;
  uint64_t last_allocation;
  uint64_t capacity;
};