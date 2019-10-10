#include "free_list_allocator.hh"

using Node = FreeListAllocator::Node;

void FreeListAllocator::init()
{
  Node* first = reinterpret_cast<Node*>(pool);
  *first      = Node{nullptr, FREELIST_ALLOCATOR_CAPACITY_BYTES};
  head        = Node{first, FREELIST_ALLOCATOR_CAPACITY_BYTES};
}

uint8_t* FreeListAllocator::allocate_bytes(unsigned size)
{
  size = (size < sizeof(Node)) ? sizeof(Node) : size;

  Node* A = &head;
  Node* B = A->next;

  while (nullptr != B)
  {
    if (B->size == size)
    {
      A->next = B->next;
      return reinterpret_cast<uint8_t*>(B);
    }
    else if (B->size > size)
    {
      B->size -= size;
      return reinterpret_cast<uint8_t*>(B) + B->size;
    }
    else
    {
      A = B;
      B = B->next;
    }
  }

  SDL_assert(false);
  return nullptr;
}

void FreeListAllocator::free_bytes(uint8_t* free_me, unsigned size)
{
  size = (size < sizeof(Node)) ? sizeof(Node) : size;

  SDL_assert(free_me);
  SDL_assert(free_me >= pool);
  SDL_assert(&free_me[size] <= &pool[FREELIST_ALLOCATOR_CAPACITY_BYTES]);

  Node* previous = &head;
  Node* current  = previous->next;

  // deallocation occured before first free node!
  if (free_me < reinterpret_cast<uint8_t*>(current))
  {
    Node* new_node = reinterpret_cast<Node*>(free_me);

    // are the two free list nodes mergable?
    if ((free_me + size) == reinterpret_cast<uint8_t*>(current))
    {
      new_node->size = size + current->size;
      new_node->next = current->next;
    }
    else
    {
      new_node->size = size;
      new_node->next = current;
    }
    previous->next = new_node;
  }
  else
  {
    while (nullptr != current)
    {
      uint8_t* begin_address = reinterpret_cast<uint8_t*>(current);
      uint8_t* end_address   = &begin_address[current->size];

      // de-allocation can't happen inside the already freed memory!
      SDL_assert(end_address <= free_me);

      uint8_t* next_address = reinterpret_cast<uint8_t*>(current->next);
      if (end_address == free_me)
      {
        current->size += size;

        // is it 3 blocks merge combo?
        if (&free_me[size] == next_address)
        {
          current->size += current->next->size;
          current->next = current->next->next;
        }
        return;
      }
      else
      {
        if (next_address > free_me)
        {
          if (&free_me[size] == next_address)
          {
            // merging case
            Node* new_node = reinterpret_cast<Node*>(free_me);
            *new_node      = *current->next;
            new_node->size += size;
            current->next = new_node;
            return;
          }
          else
          {
            // non-merging case (new node insertion)
            Node* new_node = reinterpret_cast<Node*>(free_me);
            new_node->next = current->next;
            new_node->size = size;
            current->next  = new_node;
            return;
          }
        }
      }

      previous = current;
      current  = previous->next;
    }
  }
}
