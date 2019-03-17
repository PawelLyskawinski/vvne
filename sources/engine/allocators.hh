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

  const T& operator[](const uint32_t idx) const
  {
    SDL_assert(count > idx);
    return data[idx];
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

  void reset() { count = 0; }

  T* begin() { return &data[0]; }
  T* end() { return &data[count]; }

  T        data[N];
  uint32_t count;
};

template <typename T> struct ArrayView
{
  T*  data;
  int count;

  void alloc(Stack& stack, int new_count)
  {
    data  = stack.alloc<T>(new_count);
    count = new_count;
  }

  void reset()
  {
    data  = nullptr;
    count = 0;
  }

  T&       operator[](int idx) { return data[idx]; }
  const T& operator[](int idx) const { return data[idx]; }
  T*       begin() { return data; }
  T*       end() { return &data[count]; }
  T*       begin() const { return data; }
  T*       end() const { return &data[count]; }
  bool     empty() const { return 0 == count; }
};
