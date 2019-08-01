#include "free_list_allocator.hh"
#include <algorithm>

namespace {

class FreeListIterator
{
public:
  using Node = FreeListAllocator::Node;

  FreeListIterator() = default;

  explicit FreeListIterator(Node* head)
      : current(head->next)
      , previous(head)
  {
  }

  FreeListIterator& operator++()
  {
    previous = current;
    current  = current->next;
  }

  bool  operator!=(const FreeListIterator& rhs) const { return current != rhs.current; }
  Node* operator*() { return current; }

  Node* current  = nullptr;
  Node* previous = nullptr;
};

template <typename T> uint8_t* as_address(T* in) { return reinterpret_cast<uint8_t*>(in); }

} // namespace

FreeListAllocator::FreeListAllocator()
    : head{reinterpret_cast<Node*>(this->pool), Capacity}
{
  *head.next = Node{nullptr, Capacity};
}

uint8_t* FreeListAllocator::allocate_bytes(unsigned size)
{
  size = std::min(size, static_cast<unsigned>(sizeof(Node)));
  for (FreeListIterator it(&head); nullptr != it.current; ++it)
  {
    if (it.current->size == size)
    {
      it.previous->next = it.current->next;
      return reinterpret_cast<uint8_t*>(it.current);
    }
    else if (it.current->size > size)
    {
      //
      // [ (previous) next | (current) next ########## ]
      //                |        ^
      //                *--------*
      //
      // [ (previous) next | (result) | (current) next ]
      //                |                   ^
      //                *-------------------*
      //

      it.previous->next = reinterpret_cast<Node*>(as_address(it.current) + size);
      SDL_memmove(it.previous->next, it.current, sizeof(Node));
      it.previous->next->size -= size;
      return reinterpret_cast<uint8_t*>(it.current);
    }
  }

  return nullptr;
}

void FreeListAllocator::free_bytes(uint8_t* free_me, unsigned size)
{
  size = std::min(size, static_cast<unsigned>(sizeof(Node)));

  SDL_assert(free_me);
  SDL_assert(free_me >= pool);
  SDL_assert(&free_me[size] <= &pool[Capacity]);

  FreeListIterator it(&head);
  if (free_me < as_address(it.current))
  {
    Node* new_node = reinterpret_cast<Node*>(free_me);

    //
    // are the two free list nodes mergable?
    //
    if ((free_me + size) == as_address(it.current))
    {
      *new_node = Node{it.current->next, it.current->size + size};
    }
    else
    {
      *new_node = {it.current, size};
    }

    it.previous->next = new_node;
  }
  else
  {
    for (; nullptr != it.current; ++it)
    {
      uint8_t* end_address = as_address(it.current) + it.current->size;
      Node*    next_node   = it.current->next;

      //
      // de-allocation can't happen inside the already freed memory!
      //
      SDL_assert(end_address <= free_me);

      if (end_address == free_me)
      {
        it.current->size += size;

        //
        // is it 3 blocks merge combo?
        //
        if (&free_me[size] == as_address(next_node))
        {
          it.current->size += next_node->size;
          it.current->next = next_node->next;
        }
        return;
      }
      else if (as_address(next_node) > free_me)
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

      previous = current;
      current  = previous->next;
    }
  }
}
