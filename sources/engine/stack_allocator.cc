#include "stack_allocator.hh"
#include "allocators.hh"

Stack::Stack(uint64_t new_capacity)
    : data(reinterpret_cast<uint8_t*>(SDL_malloc(new_capacity)))
    , sp(0)
    , last_allocation(0)
    , capacity(new_capacity)
{
}

Stack::~Stack()
{
  SDL_free(data);
}

void Stack::Reset()
{
  sp              = 0;
  last_allocation = 0;
}

void* Stack::Allocate(uint64_t size)
{
  last_allocation = size;
  uint8_t* r      = &data[sp];
  sp += align<uint64_t>(size);
  SDL_assert(sp < capacity);
  return r;
}

void* Stack::Reallocate(void* ptr, uint64_t size)
{
  SDL_assert(ptr == &data[sp - last_allocation]);
  SDL_assert(last_allocation < size);
  sp += (size - last_allocation);
  last_allocation = size;
  return ptr;
}

void Stack::Free(void* ptr, uint64_t size)
{
  //
  // Since we only track single last allocation size we can't provide a "pop" functionality.
  // Only allowed free is the one which frees whole stack
  //

  SDL_assert(ptr == data);
  SDL_assert(size = last_allocation);
  SDL_assert(size = sp);
  Reset();
}