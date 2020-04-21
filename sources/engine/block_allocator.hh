#pragma once

#include "vtl/multibitfield64.hh"
#include <SDL2/SDL_stdinc.h>

//
// Fixed size block allocator.
// Internal bitmap allows maximum of 1280 blocks, but will be capped depending on the available memory and block size.
//
// Allows to ONLY allocate a single block
//
struct BlockAllocator
{
public:
  void     init(uint32_t block_size, uint32_t block_count);
  void     teardown();
  uint8_t* allocate();
  void     free(const uint8_t* ptr);

  //
  // allocator visualizer helpers
  //
  [[nodiscard]] bool     is_block_used(uint64_t idx) const;
  [[nodiscard]] uint64_t calc_adjacent_blocks_count(uint64_t first) const;
  [[nodiscard]] uint64_t get_max_size() const;

  [[nodiscard]] uint32_t get_block_capacity() const
  {
    return block_capacity;
  }

  [[nodiscard]] uint32_t get_block_size() const
  {
    return block_size;
  }

private:
  uint8_t*            data               = nullptr;
  uint32_t            block_size         = 0;
  MultiBitfield64<20> block_usage_bitmap = {};
  uint32_t            block_capacity     = 0;
};
