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

namespace {

bool are_mergable(const Node* left, const Node* right)
{
  return (left->as_address() + left->size) == right->as_address();
}

} // namespace

void FreeListAllocator::free_bytes(uint8_t* free_me, unsigned size)
{
  size = (size < sizeof(Node)) ? sizeof(Node) : size;

  SDL_assert(free_me);
  SDL_assert(free_me >= pool);
  SDL_assert(&free_me[size] <= &pool[FREELIST_ALLOCATOR_CAPACITY_BYTES]);

  Node* A = &head;
  Node* B = A->next;
  Node* C = reinterpret_cast<Node*>(free_me);

  if (free_me < B->as_address())
  {
    // BEFORE:
    //     [Head] ------------------*
    //                              |
    //     [Pool] ... [free_me] ... [Node] ---> ...
    //
    // AFTER:
    //     [Head] ----*
    //                |
    //     [Pool] ... [Node] ---> [Node] ---> ...
    //
    C->size = size;
    C->next = B;

    if (are_mergable(C, B))
    {
      // BEFORE:
      //     [Head] -------------*
      //                         |
      //     [Pool] ... [free_me][Node] ---> ...
      //
      // AFTER:
      //     [Head] ----*
      //                |
      //     [Pool] ... [_____Node____] ---> ...
      //
      C->size += B->size;
      C->next = B->next;
    }

    A->next = C;
    return;
  }
  else
  {
    while (nullptr != B)
    {
      const uint8_t* end_address = B->as_address() + B->size;
      SDL_assert(end_address <= free_me);

      uint8_t* next_address = B->next->as_address();

      if (nullptr == next_address)
      {
        //
        // [Head] ----*
        //            |
        // [Pool] ... [Node] ... [free_me]
        //                 |
        //                 *---> null
        //
        if(are_mergable(B, C))
        {
          //
          // [Head] ----*
          //            |
          // [Pool] ... [_______Node_______] ---> null
          //
          B->size += size;
        }
        else
        {
          //
          // [Head] ----*
          //            |
          // [Pool] ... [Node] ---> [Node] ---> null
          //
          C->size = size;
          C->next = nullptr;
          B->next = C;
        }
        return;
      }
      else if (are_mergable(B, C))
      {
        //
        // BEFORE:
        //     [Head] ----*
        //                |
        //     [Pool] ... [Node][free_me] ... ---> ...
        //                     |              |
        //                     *--------------*
        // AFTER:
        //     [Head] ----*
        //                |
        //     [Pool] ... [_____Node____] ---> ...
        //
        B->size += size;

        if(are_mergable(B, B->next))
        {
          //
          // BEFORE:
          //     [Head] ----*
          //                |
          //     [Pool] ... [Node][free_me][Node] ---> ...
          //                     |         |
          //                     *---------*
          // AFTER:
          //     [Head] ----*
          //                |
          //     [Pool] ... [________Node_______] ---> ...
          //
          B->size += B->next->size;
          B->next = B->next->next;
        }
        return;
      }
      else if (next_address > free_me)
      {
        if (&free_me[size] == next_address)
        {
          // BEFORE:
          //     [Head] ----*
          //                |
          //     [Pool] ... [Node] ... [free_me][Node] ---> ...
          //                     |              |
          //                     *--------------*
          // AFTER:
          //     [Head] ----*
          //                |
          //     [Pool] ... [Node] --> [_____Node____] ---> ...
          //
          C->size = B->next->size + size;
          C->next = B->next->next;
          B->next = C;
          return;
        }
        else
        {
          // BEFORE:
          //     [Head] ----*
          //                |
          //     [Pool] ... [Node] ... [free_me] ... [Node] ---> ...
          //                     |                   |
          //                     *-------------------*
          //
          // AFTER:
          //     [Head] ----*
          //                |
          //     [Pool] ... [Node] ---> [Node] ----> [Node] ---> ...
          //
          C->size = size;
          C->next = B->next;
          B->next = C;
          return;
        }
      }
      else
      {
        A = B;
        B = A->next;
      }
    }
  }

  SDL_assert(false);
}
