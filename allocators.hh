#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

template <typename T> T align(T unaligned, T alignment)
{
  T result = unaligned;
  if (unaligned % alignment)
    result = unaligned + alignment - (unaligned % alignment);
  return result;
}

struct Stack
{
  void setup(uint64_t new_capacity)
  {
    data     = reinterpret_cast<uint8_t*>(SDL_malloc(new_capacity));
    sp       = 0;
    capacity = new_capacity;
  }

  template <typename T> T* alloc(int count = 1)
  {
    T* r = reinterpret_cast<T*>(&data[sp]);
    sp += align<uint64_t>(count * sizeof(T), 8);
    SDL_assert(sp < capacity);
    return r;
  }

  void reset() { sp = 0; }
  void teardown() { SDL_free(data); }

  uint8_t* data;
  uint64_t sp;
  uint64_t capacity;
};

template <typename T, uint32_t N = 64> struct ElementStack
{
  void push(const T& input)
  {
    SDL_assert(N != count);
    data[count++] = input;
  }

  void remove(const T& input)
  {
    uint32_t offset = 0;
    for (; offset < count; ++offset)
      if (data[offset] == input)
        break;

    if (count != offset)
    {
      if (offset != (count - 1))
        data[offset] = data[count - 1];
      count -= 1;
    }
  }

  T* begin() { return &data[0]; }
  T* end() { return &data[count]; }

  T        data[N];
  uint32_t count;
};
