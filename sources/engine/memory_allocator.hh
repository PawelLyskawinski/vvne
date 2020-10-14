#pragma once

#include <SDL2/SDL_stdinc.h>

class MemoryAllocator
{
public:
  virtual ~MemoryAllocator()                         = default;
  virtual void* Allocate(uint64_t size)              = 0;
  virtual void* Reallocate(void* ptr, uint64_t size) = 0;
  virtual void  Free(void* ptr, uint64_t size)       = 0;
};