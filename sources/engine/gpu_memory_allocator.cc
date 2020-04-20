#include "gpu_memory_allocator.hh"
#include "literals.hh"
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_stdinc.h>

#include <SDL2/SDL_log.h>
#include <algorithm>

void GpuMemoryAllocator::init(const VkDeviceSize init_max_size)
{
  max_size = init_max_size;
  reset();
}

void GpuMemoryAllocator::reset()
{
  nodes_count = 1;
  nodes[0]    = {0, max_size};
}

VkDeviceSize GpuMemoryAllocator::allocate_bytes(const VkDeviceSize size)
{
  Node* end = nodes + nodes_count;
  auto  it  = std::find_if(nodes, end, [size](const Node& n) { return n.size >= size; });

  SDL_assert(it != end);

  const VkDeviceSize result = it->offset;
  if (size == it->size)
  {
    std::rotate(it, it + 1, end);
  }
  else
  {
    it->offset += size;
    it->size -= size;
  }
  return result;
}

void GpuMemoryAllocator::free_bytes(VkDeviceSize offset, VkDeviceSize size)
{
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
