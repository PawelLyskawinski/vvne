#pragma once

#include <SDL2/SDL_stdinc.h>

/*
 * Extended bitfield to cover more then 64 bits. Can be used as a usage indicator for any entity based systems.
 */
struct ComponentBitfield
{
public:
  int  allocate();
  void free(int i);
  bool is_used(int i) const;

private:
  static constexpr int batches_count = 4;
  uint64_t             usage[batches_count];
};
