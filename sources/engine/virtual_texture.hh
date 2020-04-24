#pragma once

#include "math.hh"
#include "vtl/align.hh"
#include "vtl/multibitfield64.hh"
#include "vtl/span.hh"
#include <vulkan/vulkan.h>

constexpr uint32_t calculate_page_size_exponential_mips(uint32_t mips)
{
  uint32_t size = 1u;
  for (uint32_t i = 0; i < mips; ++i)
  {
    size *= 2.0f;
  }
  return size;
}

//
// _Achitecture_
//
// Main goal of this abstraction is to provide user ability to reuse single VkDescriptorSet with one texture object.
// This means less code, less micromanagement of all handles and also less descriptor set rebinds - so in theory better
// performance.
//
// Before:
// - Each draw call required descriptor set update with new texture data
// - Fragment shader uses whole sampler extent in UV space
//
// After:
// - All draw calls reuse the same descriptor set with megatexture
// - Fragment shader uses lookup buffer to see what are the UVs
//
// DEVICE will have a memory buffer for textures which we'll update depending on our needs.
// Not everything will be able to fit in there though!
//
// HOST will manage data uploads and command buffer orchiestration. When we record command buffer and pass texture
// coordinates, those coordinates will change depending on where VirtualTexture will decide to put this data.
//
// _Use_Cases_
//
// 1. User wants to draw textured geometry.
//    Asks VirtualTexture for specific LOD.
//    Draws using this LOD repeatedly.
//
// 2. User wants to draw textured geometry.
//    Asks VirtualTexture for specific LOD.
//    Draws using this LOD only for N frames.
//
// 3. Two users request very high LOD.
//    VirtualTexture can't fit those two requests into memory.
//    One of users gets lower LOD instead.
//
// _Random_Thoughts_
//
// * VirtualTexture will always know which elements are already loaded on GPU.
// * What we don't know is if those are constantly reused or not!
// * Part of data will be 'in flight' constantly so even if user requests something,
//   this request will be available only after N frames.
// * This means that extensive scheduling will have to be in place.
// * All uploaded blocks will have an additional counters and monitoring done.
//

struct BlockInfo
{
  uint32_t index;
  uint32_t started_upload_frames_age; // measures the time it took for this upload to finish
  uint32_t last_request_age;          // increments when frame changes, resets when someone requests it
};

struct GPUBlockTable
{
  Span<BlockInfo> always_loaded;
  Span<BlockInfo> streamed;
  Span<BlockInfo> ready;
};

//
// * Very low LOD level will always be guaranteed to be present in GPU memory
// * So each request will be possible to fulfill at least in a very basic sense
// * Each block (except for the baseline LOD) will have a calculated importance factor
//

float calculate_block_importance(const BlockInfo& blk)
{
  constexpr float max                  = 10.0f;
  constexpr float min                  = 0.0f;
  constexpr float request_aging_factor = 0.05;

  const float last_request_aging_contribution = blk.last_request_age * request_aging_factor;
  const float r                               = max - last_request_aging_contribution;

  return clamp(r, min, max);
}

//
// * Blocks which are not ready, or those which are persistent will not be taken into consideration when
//   selecting replacement.
// * IF everything is MAX important - user request will be silently discarded and lower LOD provided
//

BlockInfo* find_most_unimportant_block(Span<BlockInfo> ready_blocks)
{
  BlockInfo* best_fit       = ready_blocks.end();
  float      best_fit_score = 999.0f;

  BlockInfo* it = ready_blocks.begin();
  while (ready_blocks.end() != it)
  {
    const float current_score = calculate_block_importance(*it);
    if (current_score < best_fit_score)
    {
      best_fit       = it;
      best_fit_score = current_score;
    }
  }

  return best_fit;
}

//
// Textures will reserve itself space in virtual texture.
// How will this allocation be structure?
//
// Lets say we have 2 LODs: smaller one with 4 pages (2*2) and bigger one with 16 (4*4)
// Texture ALWAYS has to fit into 1 small page. This means that on a higher LOD it'll receive 2 pages.
//
// LOD 0
// * -- * -- *
// | XX |    |
// * -- * -- *
// |    |    |
// * -- * -- *
//
// LOD 1
// * -- * -- * -- * -- *
// | XX | XX |    |    |
// * -- * -- * -- * -- *
// | XX | XX |    |    |
// * -- * -- * -- * -- *
// |    |    |    |    |
// * -- * -- * -- * -- *
// |    |    |    |    |
// * -- * -- * -- * -- *
//
// Textures provided to game have to be ALREADY PREPARED to be loaded into different LOD levels.
// This means that:
//
// 1. Original size has to fit inside highest possible LOD
//
//    Let's say that page size is 128 x 128. RGBA format will force us to use 4 bytes per pixel.
//    Hence the page size is 65536_B = 64_kB
//
//    Most usual size for the texture will be about 800 x 800. We can adjust that number so that it perfectly fits into
//    some upper bound LOD.
//
//    LOD 0 - 128px
//    LOD 1 - 256px
//    LOD 2 - 512px
//    LOD 3 - 1024px
//
//    If we decide to go with 512px as the highest value, texture

struct VirtualTexture
{
  static constexpr uint32_t lod_count       = 3;
  static constexpr uint32_t pages_host_x     = 50;
  static constexpr uint32_t pages_host_y     = pages_host_x;
  static constexpr uint32_t pages_host_count = pages_host_x * pages_host_y;
  static constexpr uint32_t bytes_per_pixel  = 4; // RGBA32

  //
  // Row major layout indexing
  //  _______________
  // | 0 | 1 | 2 | 3 |
  // | 4 | 5 | 6 | 7 |
  // |___|___|___|___|
  //
  static constexpr uint32_t usage_bitfield_size = (pages_host_count / 64u) + 1;

  void     debug_dump() const;
  uint32_t calculate_all_required_memory() const;

  MultiBitfield64<usage_bitfield_size> usage;
};
