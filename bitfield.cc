#include "bitfield.hh"
#include <SDL2/SDL_assert.h>

namespace {
constexpr int ELEMENTS_IN_BATCH = 64;
} // namespace

int ComponentBitfield::allocate()
{
  //
  // First we need to find a batch with any space in it left
  //
  for (int batch_idx = 0; batch_idx < batches_count; ++batch_idx)
  {
    const uint64_t current_batch = usage[batch_idx];
    if (UINT64_MAX == current_batch)
      continue;

    //
    // Then the first offset at which 0 is encountered
    //
    for (int offset = 0; offset < ELEMENTS_IN_BATCH; ++offset)
    {
      const uint64_t mask = (uint64_t(1) << offset);
      if (current_batch & mask)
        continue;

      //
      // bit is set and offset returned from function
      //
      usage[batch_idx] |= mask;
      return (ELEMENTS_IN_BATCH * batch_idx) + offset;
    }
  }

  //
  // There is no backup. In case all bitfields are full we'll simply return first bit offset and hope for the best
  // (spoiler: it won't end well). For the sake of debug builds I'll leave an assertion so at least when something
  // goes wrong we'll know it in debug builds.
  //
  SDL_assert(false);
  return 0;
}

void ComponentBitfield::free(int i)
{
  if (0 <= i)
    usage[i / ELEMENTS_IN_BATCH] &= ~(uint64_t(1) << (i % ELEMENTS_IN_BATCH));
}

bool ComponentBitfield::is_used(int position) const
{
  const int batch_idx = position / ELEMENTS_IN_BATCH;
  SDL_assert(batch_idx < batches_count);

  const uint64_t current_batch = usage[batch_idx];
  const int      offset        = position - (ELEMENTS_IN_BATCH * batch_idx);
  const uint64_t mask          = (uint64_t(1) << offset);

  return static_cast<bool>(current_batch & mask);
}
