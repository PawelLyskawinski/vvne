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
      return B->as_address();
    }
    else if (B->size > size)
    {
      B->size -= size;
      return B->as_address() + B->size;
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

  Node* A = &head;
  Node* B = A->next;

  //
  // Deallocation occured before first free node!
  //
  if (free_me < B->as_address())
  {
    Node* C = reinterpret_cast<Node*>(free_me);
    C->size = size;
    C->next = B;

    auto are_mergable = [](const Node* left, const Node* right) {
      return (left->as_address() + left->size) == right->as_address();
    };

    if (are_mergable(C, B))
    {
      C->size += B->size;
      C->next = B->next;
    }

    A->next = C;
  }
  else
  {
    while (nullptr != B)
    {
      const uint8_t* end_address = B->as_address() + B->size;

      //
      // de-allocation can't happen inside the already freed memory!
      //
      SDL_assert(end_address <= free_me);

      uint8_t* next_address = B->next->as_address();

      if (end_address == free_me)
      {
        B->size += size;

        // is it 3 blocks merge combo?
        if (&free_me[size] == next_address)
        {
          B->size += B->next->size;
          B->next = B->next->next;
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
            Node* C = reinterpret_cast<Node*>(free_me);
            C->size += size;
            C->next = B->next->next;
            B->next = C;
            return;
          }
          else
          {
            // non-merging case (new node insertion)
            Node* C = reinterpret_cast<Node*>(free_me);
            C->size = size;
            C->next = B->next;
            B->next = C;
            return;
          }
        }
      }

      A = B;
      B = A->next;
    }
  }
}
