#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

template <typename T> constexpr T align(T unaligned, const T alignment = sizeof(uintptr_t))
{
  return (unaligned + (alignment - 1)) & (~(alignment - 1));
}

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

  void reset()
  {
    count = 0;
  }

  T* begin()
  {
    return &data[0];
  }
  T* end()
  {
    return &data[count];
  }

  T        data[N];
  uint32_t count;
};

template <typename T> struct ArrayView
{
  T*       data;
  uint32_t count;

  void reset()
  {
    data  = nullptr;
    count = 0;
  }

  void fill_with_zeros()
  {
    SDL_memset(data, 0, sizeof(T) * count);
  }

  T& operator[](uint32_t idx)
  {
    return data[idx];
  }

  const T& operator[](uint32_t idx) const
  {
    return data[idx];
  }

  T* begin()
  {
    return data;
  }

  T* end()
  {
    return &data[count];
  }

  T* begin() const
  {
    return data;

  }

  T* end() const
  {
    return &data[count];
  }

  [[nodiscard]] bool empty() const
  {
    return 0 == count;
  }
};
