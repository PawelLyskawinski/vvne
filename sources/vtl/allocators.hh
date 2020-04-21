#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>
#include "align.hh"

class Stack
{
public:
  explicit Stack(uint64_t new_capacity)
      : data(reinterpret_cast<uint8_t*>(SDL_malloc(new_capacity)))
      , sp(0)
      , capacity(new_capacity)
  {
  }

  ~Stack() { SDL_free(data); }

  void reset() { sp = 0; }

  template <typename T> T* alloc(int count = 1)
  {
    T* r = reinterpret_cast<T*>(&data[sp]);
    sp += align<uint64_t>(count * sizeof(T), 8);
    SDL_assert(sp < capacity);
    return r;
  }

private:
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

  void push(const T array[], uint32_t array_n)
  {
    SDL_assert(N > (count + array_n));
    for (uint32_t i = 0; i < array_n; ++i)
      data[count++] = array[i];
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

