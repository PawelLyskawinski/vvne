#include "virtual_texture.hh"
#include "literals.hh"
#include <SDL2/SDL_log.h>

static void print_bytes(const char* issuer, uint32_t bytes)
{
  if (1_MB < bytes)
  {
    SDL_Log("%s: %u_MB", issuer, bytes / (1024u * 1024u));
  }
  else
  {
    SDL_Log("%s: %u_KB", issuer, bytes / 1024u);
  }
}

void VirtualTexture::debug_dump() const
{
  SDL_Log("VirtualTexture::mips_count: %u", mips_count);
  print_bytes("total_size", calculate_all_required_memory());
}

uint32_t VirtualTexture::calculate_all_required_memory() const
{
  uint32_t total_size = 0u;
  for (uint32_t i = 0; i < mips_count; ++i)
  {
    const uint32_t page_size           = calculate_page_size_exponential_mips(i);
    const uint32_t memory_size_for_mip = pages_host_count * page_size * page_size;

    char mip_name[64];
    SDL_snprintf(mip_name, 64, "mip%u [%u x %u]", mips_count - i - 1, page_size, page_size);
    print_bytes(mip_name, memory_size_for_mip);

    total_size += memory_size_for_mip;
  }
  return total_size;
}
