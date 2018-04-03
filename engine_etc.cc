#include "engine.hh"
#include <SDL2/SDL_assert.h>
#include <stb_image.h>

uint32_t find_memory_type_index(VkPhysicalDeviceMemoryProperties* properties, VkMemoryRequirements* reqs,
                                VkMemoryPropertyFlags searched)
{
  for (uint32_t i = 0; i < properties->memoryTypeCount; ++i)
  {
    if (0 == (reqs->memoryTypeBits & (1 << i)))
      continue;

    VkMemoryPropertyFlags memory_type_properties = properties->memoryTypes[i].propertyFlags;

    if (searched == (memory_type_properties & searched))
    {
      return i;
    }
  }

  // this code fragment should never be reached!
  SDL_assert(false);
  return 0;
}




