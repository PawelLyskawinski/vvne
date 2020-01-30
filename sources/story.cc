#include "engine/allocators.hh"
#include "engine/hierarchical_allocator.hh"
#include "story_editor.hh"

namespace story {

namespace {

template <typename T1, typename T2, typename TPredicate>
uint32_t accumulate_indices(const T1* begin, const T1* end, T2* dst, TPredicate pred)
{
  const T2* dst_begin = dst;
  for (const T1* it = begin; it != end; ++it)
  {
    if (pred(*begin))
    {
      *dst = static_cast<T2>(it - begin);
      dst += 1;
    }
  }
  return static_cast<uint32_t>(dst - dst_begin);
}

uint32_t gather_active_entities(const State states[], uint32_t count, uint32_t* dst_indicies)
{
  auto is_active = [](State state) { return State::Active == state; };
  return accumulate_indices(states, states + count, dst_indicies, is_active);
}

} // namespace

void tick(HierarchicalAllocator& allocator, Data& data)
{
  //
  // 1. gather all "active" nodes.
  // 2. for each
  //    2.1 check if they can progress
  //        if yes -> make the next nodes active and add them to the list
  //

  uint32_t                  active_entites_capacity = 256;
  uint32_t*                 active_entities         = allocator.allocate_threadsafe<uint32_t>(active_entites_capacity);
  [[maybe_unused]] uint32_t active_entities_count =
      gather_active_entities(data.node_states, data.entity_count, active_entities);

  // do the work

  allocator.free_threadsafe(active_entities, active_entites_capacity);
}

} // namespace story
