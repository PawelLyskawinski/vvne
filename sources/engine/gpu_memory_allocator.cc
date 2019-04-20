#include "gpu_memory_allocator.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_stdinc.h>

void GpuMemoryAllocator::init(VkDeviceSize max_size)
{
  nodes_count     = 1;
  nodes[0].offset = 0;
  nodes[0].size   = max_size;
  this->max_size  = max_size;
}

class DestructorPrinter
{
public:
  explicit DestructorPrinter(const char* issuer, uint32_t& observed)
      : m_issuer(issuer)
      , m_observed(observed)
  {
  }
  ~DestructorPrinter() { SDL_Log("%s - %u", m_issuer, m_observed); }

private:
  const char* m_issuer;
  uint32_t&   m_observed;
};

VkDeviceSize GpuMemoryAllocator::allocate_bytes(VkDeviceSize size)
{
  // DestructorPrinter p(__FUNCTION__, nodes_count);
  for (uint32_t i = 0; i < nodes_count; ++i)
  {
    Node& current_node = nodes[i];
    if (current_node.size > size)
    {
      VkDeviceSize result = current_node.offset;
      current_node.offset += size;
      current_node.size -= size;
      return result;
    }
    else if (current_node.size == size)
    {
      VkDeviceSize result = current_node.offset;
      SDL_memmove(&nodes[i], &nodes[i + 1], sizeof(Node) * (nodes_count - i - 1));
      return result;
    }
  }

  // Reaching this point means it's impossible to perform allocation.
  // Extend the buffer!
  SDL_assert(false);

  return 0;
}

void GpuMemoryAllocator::free_bytes(VkDeviceSize offset, VkDeviceSize size)
{
  //DestructorPrinter p(__FUNCTION__, nodes_count);

  // basically a sorted container insertion problem
  const Node insertion = {offset, size};

  // TODO catch double frees

  if (0 == nodes_count) // whole memory used, inserting first element to list
  {
    nodes_count = 1;
    nodes[0]    = insertion;
  }
  else if (offset < nodes[0].offset) // insertion before the first element on list
  {
    if ((offset + size) == nodes[0].offset)
    {
      nodes[0].offset = offset;
      nodes[0].size += size;
    }
    else
    {
      SDL_memmove(&nodes[1], &nodes[0], sizeof(Node) * nodes_count);
      nodes[0] = insertion;
      nodes_count++;
    }
  }
  // else if (1 == nodes_count) // insertion as the second list element
  //{
  //  nodes_count = 2;
  //  nodes[1]    = insertion;
  //}
  else
  {
    for (uint32_t i = 0; i < (nodes_count - 1); ++i)
    {
      Node& before = nodes[i + 0];
      Node& after  = nodes[i + 1];

      if (offset + size > after.offset)
        continue;

      // just continue if the offset range is not fitting
      if (after.offset < offset)
        continue;

      if ((before.offset + before.size) == offset) // left merge
      {
        before.size += size;
        return;
      }
      else if (after.offset == (offset + size)) // right merge
      {
        after.offset = offset;
        after.size += size;
        return;
      }
      else // insertion between elements
      {
        SDL_memmove(&nodes[i + 2], &nodes[i + 1], sizeof(Node) * (nodes_count - (i + 1)));
        nodes[i + 1] = insertion;
        return;
      }
    }

    // if we got to the end of list, insertion happens as the new last element on list
    nodes[nodes_count++] = insertion;
  }
}
