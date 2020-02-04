#pragma once

#include "engine/allocators.hh"
#include "engine/hierarchical_allocator.hh"
#include "story_components.hh"
#include <SDL2/SDL_rwops.h>

namespace story {

struct Story
{
  static constexpr uint32_t entities_capacity    = 256;
  static constexpr uint32_t components_capacity  = 64;
  static constexpr uint32_t connections_capacity = 10'240;

  Node*           nodes                  = nullptr;
  State*          node_states            = nullptr;
  uint32_t        entity_count           = 0;
  TargetPosition* target_positions       = nullptr;
  uint32_t        target_positions_count = 0;
  Connection*     connections            = nullptr;
  uint32_t        connections_count      = 0;

  void setup(HierarchicalAllocator& allocator);
  void teardown(HierarchicalAllocator& allocator);
  void load(SDL_RWops* handle);
  void save(SDL_RWops* handle);
  void push_connection(const Connection& conn);
  void dump_connections() const;
  void reset_graph_state();
  void tick(Stack& allocator);
};

} // namespace story
