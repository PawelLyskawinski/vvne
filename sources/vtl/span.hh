#pragma once

#include <SDL2/SDL_stdinc.h>

template <typename T> struct Span
{
  T*       data  = nullptr;
  unsigned count = 0;

  Span() = default;

  Span(T* data, unsigned count)
      : data(data)
      , count(count)
  {
  }

  template <unsigned N>
  explicit Span(T (&t)[N])
      : data(t)
      , count(N)
  {
  }

  void fill_with_zeros()
  {
    SDL_memset(data, 0, sizeof(T) * count);
  }

  T& operator[](unsigned idx)
  {
    return data[idx];
  }

  const T& operator[](unsigned idx) const
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
