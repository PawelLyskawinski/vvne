#include "free_list_allocator.hh"
#include "allocators.hh"

using Node = FreeListAllocator::Node;

FreeListAllocator::FreeListAllocator(uint64_t new_capacity)
    : head{}
    , pool(reinterpret_cast<uint8_t*>(SDL_malloc(new_capacity)))
    , capacity(new_capacity)
{
  Node* first = reinterpret_cast<Node*>(pool);
  *first      = Node{nullptr, static_cast<uint32_t>(capacity)};
  head        = Node{first, static_cast<uint32_t>(capacity)};
}

FreeListAllocator::~FreeListAllocator()
{
  SDL_free(pool);
}

void* FreeListAllocator::Allocate(uint64_t size)
{
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

void* FreeListAllocator::Reallocate(void* ptr, uint64_t size)
{
  // TODO implement!
  (void)ptr;
  (void)size;
  SDL_assert(false);
  return ptr;
}

namespace {

bool are_mergable(const Node* left, const Node* right)
{
  return (left->as_address() + left->size) == right->as_address();
}

} // namespace

void FreeListAllocator::Free(void* ptr, uint64_t size)
{
  uint8_t* free_me = reinterpret_cast<uint8_t*>(ptr);
  SDL_assert(free_me);
  SDL_assert(pool <= free_me);
  SDL_assert(&free_me[size] <= &pool[capacity]);

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
        if (are_mergable(B, C))
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

        if (are_mergable(B, B->next))
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
