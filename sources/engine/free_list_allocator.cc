#include "free_list_allocator.hh"

using Node = FreeListAllocator::Node;

static void push_node(uint8_t* new_node, unsigned new_node_size, Node* previous)
{
  uint8_t* begin_previous = reinterpret_cast<uint8_t*>(previous);
  uint8_t* end_previous   = begin_previous + previous->size;
  Node*    potential_node = reinterpret_cast<Node*>(new_node);

  if (end_previous == new_node)
  {
    // If the freed memory sitting at the end of current node,
    // we'll simply extend the current node
    previous->size += new_node_size;
  }
  else
  {
    // there is some still allocated space to take into consideration
    potential_node->next = previous->next;
    potential_node->size = new_node_size;
    previous->next       = potential_node;
  }
}

void FreeListAllocator::init()
{
  Node* first_element = reinterpret_cast<Node*>(pool);
  first_element->next = nullptr;
  first_element->size = FREELIST_ALLOCATOR_CAPACITY_BYTES;

  head.next = first_element;
  head.size = FREELIST_ALLOCATOR_CAPACITY_BYTES;
}

uint8_t* FreeListAllocator::allocate_bytes(unsigned size)
{
  Node* previous = &head;
  Node* current  = previous->next;

  while (nullptr != current)
  {
    if (current->size == size)
    {
      previous->next = current->next;
      return reinterpret_cast<uint8_t*>(current);
    }
    else if (current->size > size)
    {
      uint8_t*       old_pointer   = reinterpret_cast<uint8_t*>(current);
      uint8_t*       new_pointer   = old_pointer + size;
      Node*          next          = current->next;
      const unsigned shrinked_size = current->size - size;

      current        = reinterpret_cast<Node*>(new_pointer);
      current->size  = shrinked_size;
      current->next  = next;
      previous->next = current;
      return old_pointer;
    }
    else
    {
      previous = current;
      current  = previous->next;
    }
  }

  return nullptr;
}

void FreeListAllocator::free_bytes(uint8_t* free_me, unsigned count)
{
  if (nullptr == free_me)
    return;

  // we don't want to free pointers not in this memory pool
  SDL_assert((free_me >= pool) and (&free_me[count] <= &pool[FREELIST_ALLOCATOR_CAPACITY_BYTES]));

  Node* previous       = &head;
  Node* current        = previous->next;
  Node* potential_node = reinterpret_cast<Node*>(free_me);

  while (nullptr != current)
  {
    uint8_t* begin_address = reinterpret_cast<uint8_t*>(current);
    uint8_t* end_address   = &begin_address[current->size];

    // de-allocation can't happen inside the already freed memory!
    SDL_assert(end_address >= free_me);

    uint8_t* next_address = reinterpret_cast<uint8_t*>(current->next);

    if (current->next)
    {
      if (free_me > next_address)
      {
        push_node(free_me, count, current);
        return;
      }
    }
    else
    {
      // End of the line, it has to be it!
      push_node(free_me, count, current);
      return;
    }

    previous = current;
    current  = previous->next;
  }
}
