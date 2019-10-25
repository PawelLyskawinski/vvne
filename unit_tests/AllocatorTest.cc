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

char number_to_char(uint8_t v) { return (v < 10) ? ('0' + v) : ('A' + (v - 10)); }

void print_mem(const uint8_t* data, const uint32_t size)
{
  char* buffer = reinterpret_cast<char*>(SDL_malloc(3 * size + 1));

  for (uint32_t i = 0; i < size; ++i)
  {
    const uint8_t byte  = data[i];
    buffer[(3 * i) + 0] = number_to_char((byte & 0xF0) >> 4);
    buffer[(3 * i) + 1] = number_to_char((byte & 0x0F));
    buffer[(3 * i) + 2] = ' ';
  }

  buffer[3 * size] = '\0';
  SDL_Log("%s", buffer);
}

struct Allocation
{
  uint8_t* ptr;
  uint32_t size;
};

void validate(const Allocation* begin, const Allocation* end)
{
  for (const Allocation* a = begin; end != a; ++a)
  {
    for (const Allocation* b = (a + 1); end != b; ++b)
    {
      SDL_assert(a->ptr != b->ptr);

      auto is_between = [](const uint8_t* a, const uint8_t* b, const uint8_t* c) { return (c >= a) && (c < b); };

      SDL_assert(!is_between(a->ptr, a->ptr + a->size, b->ptr));
      SDL_assert(!is_between(b->ptr, b->ptr + b->size, a->ptr));
    }
  }
}

int main()
{
  SDL_Log("INITIAL SIZE: %u", FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES);
  FreeListAllocator* allocator = reinterpret_cast<FreeListAllocator*>(SDL_calloc(1, sizeof(FreeListAllocator)));
  SDL_assert(is_memory_zeroed(allocator->pool, allocator->pool + FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES));

  allocator->init();
  constexpr uint32_t CAPACITY = 0x02;

  Allocation allocs[CAPACITY] = {};

  for (uint32_t i = 0; i < CAPACITY; ++i)
  {
    allocs[i].size = 30 * (i + 1);
  }

  SDL_Log("Allocating memory ... ");
  for (uint8_t i = 0; i < CAPACITY; ++i)
  {
    Allocation& a = allocs[i];
    SDL_Log("Allocating %u bytes and filling memory with 0x%X", a.size, i + 1);
    a.ptr = allocator->allocate<uint8_t>(a.size);
    validate(allocs, &allocs[i + 1]);
    SDL_assert(is_memory_zeroed(a.ptr, a.ptr + a.size));
    std::fill(a.ptr, a.ptr + a.size, i + 1);
  }
  SDL_Log("Allocating memory ... DONE");

  print_mem(allocator->pool, FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES);

  for (uint32_t i = 0; i < CAPACITY / 2; i += 2)
  {
    std::swap(allocs[i], allocs[CAPACITY - i - 1]);
  }

  SDL_Log("Freeing memory ... ");
  for (Allocation& a : allocs)
  {
    SDL_assert(same_value_in_memory(a.ptr, a.ptr + a.size));
    std::fill(a.ptr, a.ptr + a.size, 0u);
    allocator->free(a.ptr, a.size);
  }
  SDL_Log("Freeing memory ... DONE");

  print_mem(allocator->pool, FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES);

  SDL_assert(is_memory_zeroed(allocator->pool + sizeof(FreeListAllocator::Node), allocator->pool + FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES));
  SDL_assert(reinterpret_cast<uint8_t*>(allocator->head.next) == allocator->pool);
  SDL_assert(FreeListAllocator::FREELIST_ALLOCATOR_CAPACITY_BYTES == allocator->head.next->size);

  SDL_free(allocator);
  return 0;
}