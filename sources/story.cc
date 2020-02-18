#include "story.hh"
#include "engine/allocators.hh"
#include "engine/fileops.hh"
#include "player.hh"
#include <SDL2/SDL_log.h>
#include <algorithm>

namespace story {

namespace {

template <typename T1, typename T2, typename TPredicate>
uint32_t accumulate_indices(const T1* begin, const T1* end, T2* dst, TPredicate pred)
{
  T2* dst_begin = dst;
  for (const T1* it = begin; it != end; ++it)
  {
    if (pred(*it))
    {
      *dst = static_cast<T2>(it - begin);
      dst += 1;
    }
  }
  return static_cast<uint32_t>(std::distance(dst_begin, dst));
}

uint32_t gather_active_entities(const State states[], uint32_t count, uint32_t* dst_indicies)
{
  auto is_active = [](State state) { return State::Active == state; };
  return accumulate_indices(states, states + count, dst_indicies, is_active);
}

} // namespace

void Story::setup(HierarchicalAllocator& hallocator)
{
  allocator        = &hallocator;
  nodes            = hallocator.allocate<Node>(entities_capacity);
  node_states      = hallocator.allocate<State>(entities_capacity);
  target_positions = hallocator.allocate<TargetPosition>(components_capacity);
  connections      = hallocator.allocate<Connection>(connections_capacity);
  dialogues        = hallocator.allocate<Dialogue>(dialogues_capacity);
}

void Story::teardown()
{
  allocator->free(nodes, entities_capacity);
  allocator->free(node_states, entities_capacity);
  allocator->free(target_positions, components_capacity);
  allocator->free(connections, connections_capacity);
  allocator->free(dialogues, dialogues_capacity);
}

void Story::load(SDL_RWops* handle)
{
  FileOps s(handle);

  s.deserialize(entity_count);
  s.deserialize(nodes, entity_count);
  s.deserialize(target_positions_count);
  s.deserialize(target_positions, target_positions_count);
  s.deserialize(connections_count);
  s.deserialize(connections, connections_count);

  s.deserialize(dialogues_count);
  for (uint32_t i = 0; i < dialogues_count; ++i)
  {
    Dialogue& dialogue = dialogues[i];
    s.deserialize(dialogue.entity);
    s.deserialize(dialogue.type);
    dialogue.text = allocator->allocate<char>(Dialogue::type_to_size(dialogue.type));
    s.deserialize(dialogue.text, Dialogue::type_to_size(dialogue.type));
  }

  reset_graph_state();
}

void Story::save(SDL_RWops* handle)
{
  FileOps s(handle);

  s.serialize(entity_count);
  s.serialize(nodes, entity_count);
  s.serialize(target_positions_count);
  s.serialize(target_positions, target_positions_count);
  s.serialize(connections_count);
  s.serialize(connections, connections_count);

  s.serialize(dialogues_count);
  for (uint32_t i = 0; i < dialogues_count; ++i)
  {
    const Dialogue& dialogue = dialogues[i];
    s.serialize(dialogue.entity);
    s.serialize(dialogue.type);
    s.serialize(dialogue.text, Dialogue::type_to_size(dialogue.type));
  }
}

void Story::push_connection(const Connection& new_connection)
{
  Connection* connections_end = &connections[connections_count];
  auto        it              = std::find(connections, connections_end, new_connection);

  if (connections_end != it)
  {
    std::rotate(it, it + 1, connections_end);
    --connections_count;
  }
  else
  {
    *connections_end = new_connection;
    connections_count += 1;
  }
}

void Story::dump_connections() const
{
  std::for_each(connections, connections + connections_count, [](const Connection& c) {
    SDL_Log("src_node_idx: %u, src_output_idx: %u, dst_input_idx: %u, dst_node_idx: %u", c.src_node_idx,
            c.src_output_idx, c.dst_input_idx, c.dst_node_idx);
  });
}

void Story::validate_and_fix()
{
  for (uint32_t entity = 0; entity < entity_count; ++entity)
  {
    if (Node::GoTo == nodes[entity])
    {
      //
      // Each GoTo node should have only one corresponding "TargetPosition" component
      //
      TargetPosition* begin = target_positions;
      TargetPosition* end   = target_positions + target_positions_count;

      uint32_t n = std::count(begin, end, entity);

      if (1 < n)
      {
        SDL_Log("Found %u TargetPosition components for a single GoTo block (%u). Attempting to fix by removing all "
                "other ones except for the first",
                n, entity);

        end                    = std::remove(std::find(begin, end, entity) + 1, end, entity);
        target_positions_count = std::distance(begin, end);
      }
      else if (0 == n)
      {
        SDL_Log("GoTo block (%u) does not have any corresponding TargetPosition component! Attempting to fix by adding "
                "stub",
                entity);

        const TargetPosition stub = {
            .entity   = entity,
            .position = Vec3(0.0f, 0.0f, 0.0f),
            .radius   = 1.0f,
        };

        *end = stub;
        target_positions_count += 1;
      }
    }
    else if (Node::Dialogue == nodes[entity])
    {
      //
      // Each Dialogue node should have only one corresponding "Dialogue" component
      //
      Dialogue* begin = dialogues;
      Dialogue* end   = dialogues + dialogues_count;

      uint32_t n = std::count(begin, end, entity);

      if (1 < n)
      {
        SDL_Log("Found %u Dialogue components for a single Dialogue block (%u). Attempting to fix by removing all "
                "other ones except for the first",
                n, entity);

        end             = std::remove(std::find(begin, end, entity) + 1, end, entity);
        dialogues_count = std::distance(begin, end);
      }
      else if (0 == n)
      {
        SDL_Log("Dialogue block (%u) does not have any corresponding Dialogue component! Attempting to fix by adding "
                "stub",
                entity);

        const Dialogue stub = {
            .entity = entity,
            .type   = Dialogue::Type::Short,
            .text   = allocator->allocate<char>(Dialogue::type_to_size(Dialogue::Type::Short)),
        };

        *end = stub;
        dialogues_count += 1;
      }
    }
  }
} // namespace story

void Story::reset_graph_state()
{
  std::fill(node_states, node_states + entity_count, State::Upcoming);
  auto it = std::find(nodes, nodes + entity_count, Node::Start);
  SDL_assert((nodes + entity_count) != it);
  node_states[std::distance(nodes, it)] = State::Active;
}

void Story::depth_first_cancel(const Connection& connection)
{
  const uint32_t investigated = connection.src_node_idx;
  switch (node_states[investigated])
  {
  case State::Upcoming:
  case State::Active: {
    node_states[investigated] = State::Cancelled;
    depth_first_cancel(investigated);
    break;
  }
  case State::Cancelled:
  case State::Finished:
    break;
  }
}

void Story::depth_first_cancel(uint32_t entity)
{
  auto              is_parent = [entity](const Connection& c) { return entity == c.dst_node_idx; };
  const Connection* begin     = connections;
  const Connection* end       = connections + connections_count;
  for (const Connection* it = std::find_if(begin, end, is_parent); end != it; it = std::find_if(it + 1, end, is_parent))
  {
    depth_first_cancel(*it);
  }
}

bool Story::update(const Player& player, uint32_t entity_idx)
{
  switch (nodes[entity_idx])
  {
  case Node::Start:
    node_states[entity_idx] = State::Finished;
    return false;
  case Node::Any:
    node_states[entity_idx] = State::Finished;
    return false;
  case Node::All: {
    auto is_connection_satisfying_all = [&](const Connection& connection) {
      if (entity_idx == connection.dst_node_idx)
      {
        return State::Finished == node_states[connection.src_node_idx];
      }
      else
      {
        return true;
      }
    };
    if (std::all_of(connections, connections + connections_count, is_connection_satisfying_all))
    {
      node_states[entity_idx] = State::Finished;
      return false;
    }
    else
    {
      return true;
    }
  }
  case Node::GoTo: {
    const TargetPosition* co = std::find(target_positions, target_positions + target_positions_count, entity_idx);
    SDL_assert(co);
    if ((player.position - co->position).len() >= co->radius)
    {
      return true;
    }
    else
    {
      SDL_Log("GoTo reached!");
      node_states[entity_idx] = State::Finished;
      return false;
    }
  }
  case Node::Dialogue: {
    node_states[entity_idx] = State::Finished;
    const Dialogue* co = std::find(dialogues, dialogues + dialogues_count, entity_idx);
    active_dialogue = co;
    return false;
  }
  default:
    return true;
  }
}

void Story::tick(const Player& player, Stack& allocator)
{
  uint32_t  active_entites_capacity = 256;
  uint32_t* active_entities         = allocator.alloc<uint32_t>(active_entites_capacity);
  uint32_t  active_entities_count   = gather_active_entities(node_states, entity_count, active_entities);

  //
  // partition active entities into those which are still active and already finished
  //
  // [ A A A A A ] --> [ A A F F F ]
  //                         *
  //                         partition point
  //

  auto      call_update     = [&](uint32_t entity_idx) { return update(player, entity_idx); };
  uint32_t* partition_point = std::partition(active_entities, active_entities + active_entities_count, call_update);
  uint32_t  finished_count  = std::distance(partition_point, active_entities + active_entities_count);

  while (finished_count)
  {
    //
    // Is any of "finished" nodes "any"? If so, cancel all input connections in a dfs fashion
    //
    for (uint32_t* it = partition_point; (partition_point + finished_count) != it; ++it)
    {
      if (Node::Any == nodes[*it])
      {
        depth_first_cancel(*it);
      }
    }

    //
    // [ A A F F F ] --> [ A A F F F A_new A_new A_new ]
    //                               *
    //                               new_active

    uint32_t* new_active              = active_entities + active_entities_count;
    uint32_t* new_active_accummulator = new_active;

    for (uint32_t connection_idx = 0; connection_idx < connections_count; ++connection_idx)
    {
      const Connection& c = connections[connection_idx];
      if (std::any_of(partition_point, partition_point + finished_count,
                      [c](uint32_t entity_idx) { return c.src_node_idx == entity_idx; }))
      {
        *new_active_accummulator++  = c.dst_node_idx;
        node_states[c.dst_node_idx] = State::Active;
      }
    }

    if (new_active != new_active_accummulator)
    {
      //
      // [ A A F F F A_new A_new A_new ] --> [ A_new A_new A_new ]
      //
      active_entities_count = std::distance(new_active, new_active_accummulator);
      std::copy(new_active, new_active_accummulator, active_entities);

      //
      // [ A_new A_new A_new ] --> [ A_new A_new A_new F_new F_new F_new ]
      //
      partition_point = std::partition(active_entities, active_entities + active_entities_count, call_update);
      finished_count  = std::distance(partition_point, active_entities + active_entities_count);
    }
    else
    {
      finished_count = 0;
    }
  }
}

} // namespace story
