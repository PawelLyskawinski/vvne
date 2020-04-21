#pragma once

#include <SDL2/SDL_atomic.h>

template <typename T, int SIZE> struct AtomicStack
{
public:
  void push(const T& in) { stack[SDL_AtomicIncRef(&count)] = in; }
  T*   begin() { return stack; }
  T*   end() { return &stack[SDL_AtomicGet(&count)]; }
  void reset() { SDL_AtomicSet(&count, 0); }

  [[nodiscard]] uint32_t size() { return SDL_AtomicGet(&count); }

private:
  T            stack[SIZE];
  SDL_atomic_t count;
};
