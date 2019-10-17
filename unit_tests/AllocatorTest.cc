#define SDL_MAIN_HANDLED
#include "../sources/engine/free_list_allocator.hh"
#include <SDL2/SDL.h>
#include <algorithm>

void print_as_offset(FreeListAllocator* allocator, uint8_t* ptr)
{
  SDL_Log("%u", static_cast<uint32_t>(ptr - allocator->pool));
}

void print_freelist(FreeListAllocator* allocator)
{
  SDL_Log("BEGIN");
  FreeListAllocator::Node* current = allocator->head.next;
  while (current)
  {
    SDL_Log("offset: %u, size: %u", static_cast<uint32_t>(reinterpret_cast<uint8_t*>(current) - allocator->pool),
            current->size);
    current = current->next;
  }
  SDL_Log("END");
}

template <typename T> bool is_memory_zeroed(const T* begin, const T* end)
{
  return std::none_of(begin, end, [](const T& elem) { return T(0) != elem; });
}

template <typename T> bool same_value_in_memory(const T* begin, const T* end)
{
  const T val = *begin;
  return std::all_of(begin, end, [val](const T& elem) { return val == elem; });
}

int main()
{
  SDL_Log("INITIAL SIZE: %u", FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES);
  FreeListAllocator* allocator = reinterpret_cast<FreeListAllocator*>(SDL_calloc(1, sizeof(FreeListAllocator)));
  SDL_assert(is_memory_zeroed(allocator->pool, allocator->pool + FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES));

  allocator->init();
  constexpr uint32_t CAPACITY = 0xFA;

  struct Allocation
  {
    uint8_t* ptr;
    uint32_t size;
  };

  Allocation allocs[CAPACITY];

  for (uint32_t i = 0; i < CAPACITY; ++i)
  {
    allocs[i].size = 15 * i;
  }

  for (uint8_t i = 0; i < CAPACITY; ++i)
  {
    Allocation& a = allocs[i];
    SDL_Log("Allocating %u bytes and filling memory with 0x%X", a.size, i+1);
    a.ptr         = allocator->allocate<uint8_t>(a.size);
    SDL_assert(is_memory_zeroed(a.ptr, a.ptr + a.size));
    std::fill(a.ptr, a.ptr + a.size, i);
  }

  for (uint32_t i = 0; i < CAPACITY / 2; i += 2)
  {
    std::swap(allocs[i], allocs[CAPACITY - i - 1]);
  }

  for (Allocation& a : allocs)
  {
    SDL_assert(same_value_in_memory(a.ptr, a.ptr + a.size));
    std::fill(a.ptr, a.ptr + a.size, 0u);
    allocator->free(a.ptr, a.size);
  }

  SDL_assert(is_memory_zeroed(allocator->pool, allocator->pool + FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES));
  SDL_assert(reinterpret_cast<uint8_t*>(allocator->head.next) == allocator->pool);
  SDL_assert(FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES == allocator->head.next->size);

  SDL_free(allocator);
  return 0;
}