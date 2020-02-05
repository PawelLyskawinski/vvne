#pragma once

#include <SDL2/SDL_rwops.h>

struct FileOps
{
  explicit FileOps(SDL_RWops* handle)
      : handle(handle)
  {
  }

  template <typename T> void serialize(const T& data)
  {
    SDL_RWwrite(handle, &data, sizeof(T), 1);
  }

  template <typename T> void serialize(const T* data, uint32_t count)
  {
    SDL_RWwrite(handle, data, sizeof(T), count);
  }

  template <typename T> void deserialize(T& data)
  {
    SDL_RWread(handle, &data, sizeof(T), 1);
  }

  template <typename T> void deserialize(T* data, uint32_t count)
  {
    SDL_RWread(handle, data, sizeof(T), count);
  }

  SDL_RWops* handle;
};
