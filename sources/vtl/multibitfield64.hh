#pragma once

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

template <uint32_t COUNT_64BITFIELDS> struct MultiBitfield64
{
  void set(uint64_t offset)
  {
    const uint64_t data_index  = offset / 64u;
    const uint64_t data_offset = offset % 64u;
    SDL_assert(data_index < COUNT_64BITFIELDS);
    data[data_index] |= (uint64_t(1) << data_offset);
  }

  void clear(uint64_t offset)
  {
      const uint64_t data_index  = offset / 64u;
      const uint64_t data_offset = offset % 64u;
      SDL_assert(data_index < COUNT_64BITFIELDS);
      data[data_index] &= ~((uint64_t(1) << data_offset));
  }

  [[nodiscard]] bool test(uint64_t offset) const
  {
    const uint64_t data_index  = offset / 64u;
    const uint64_t data_offset = offset % 64u;
    SDL_assert(data_index < COUNT_64BITFIELDS);
    return data[data_index] & (uint64_t(1) << data_offset);
  }

  void clear()
  {
    for (uint64_t& it : data)
    {
      it = 0;
    }
  }

  uint64_t data[COUNT_64BITFIELDS] = {};
};