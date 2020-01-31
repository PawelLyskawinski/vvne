#include "engine/allocators.hh"
#include "engine/hierarchical_allocator.hh"
#include "story_editor.hh"
#include <algorithm>

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

//
// Returns:
// true  - node still active
// false - node finished executing
//
bool update(Data& data, uint32_t entity_idx)
{
  switch (data.nodes[entity_idx])
  {
  case Node::Start:
    data.node_states[entity_idx] = State::Finished;
    return false;
  default:
    return true;
  }
}

} // namespace

void tick(HierarchicalAllocator& allocator, Data& data)
{
  uint32_t  active_entites_capacity = 256;
  uint32_t* active_entities         = allocator.allocate_threadsafe<uint32_t>(active_entites_capacity);
  uint32_t  active_entities_count   = gather_active_entities(data.node_states, data.entity_count, active_entities);

  //
  // partition active entities into those which are still active and already finished
  //
  // [ A A A A A ] --> [ A A F F F ]
  //                         *
  //                         partition point
  //

  auto      call_update     = [&data](uint32_t entity_idx) { return update(data, entity_idx); };
  uint32_t* partition_point = std::partition(active_entities, active_entities + active_entities_count, call_update);
  uint32_t  finished_count  = std::distance(partition_point, active_entities + active_entities_count);

  while (finished_count)
  {
    //
    // [ A A F F F ] --> [ A A F F F A_new A_new A_new ]
    //                               *
    //                               new_active

    uint32_t* new_active              = active_entities + active_entities_count;
    uint32_t* new_active_accummulator = new_active;
    for (uint32_t connection_idx = 0; connection_idx < data.connections_count; ++connection_idx)
    {
      const Connection& c         = data.connections[connection_idx];
      auto              is_source = [c](uint32_t entity_idx) { return c.src_node_idx == entity_idx; };
      if (std::any_of(partition_point, partition_point + finished_count, is_source))
      {
        *new_active_accummulator++ = c.dst_node_idx;
      }
    }

    if (new_active != new_active_accummulator)
    {
      active_entities_count = std::distance(new_active, new_active_accummulator);
      std::copy(new_active, new_active_accummulator, active_entities);
      partition_point = std::partition(active_entities, active_entities + active_entities_count, call_update);
      finished_count  = std::distance(partition_point, active_entities + active_entities_count);
    }
    else
    {
      finished_count = 0;
    }
  }

  allocator.free_threadsafe(active_entities, active_entites_capacity);
}

} // namespace story
