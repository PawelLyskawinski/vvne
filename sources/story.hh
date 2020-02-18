#pragma once

#include "engine/allocators.hh"
#include "engine/hierarchical_allocator.hh"
#include "story_components.hh"
#include <SDL2/SDL_rwops.h>

struct Player;

namespace story {

struct Story
{
  static constexpr uint32_t entities_capacity    = 256;
  static constexpr uint32_t components_capacity  = 64;
  static constexpr uint32_t dialogues_capacity   = 1024;
  static constexpr uint32_t connections_capacity = 10'240;

  HierarchicalAllocator* allocator              = nullptr;
  Node*                  nodes                  = nullptr;
  State*                 node_states            = nullptr;
  uint32_t               entity_count           = 0;
  TargetPosition*        target_positions       = nullptr;
  uint32_t               target_positions_count = 0;
  Connection*            connections            = nullptr;
  uint32_t               connections_count      = 0;
  Dialogue*              dialogues              = nullptr;
  uint32_t               dialogues_count        = 0;
  const Dialogue*        active_dialogue        = nullptr;

  void setup(HierarchicalAllocator& allocator);
  void teardown();
  void load(SDL_RWops* handle);
  void save(SDL_RWops* handle);
  void push_connection(const Connection& conn);
  void dump_connections() const;
  void validate_and_fix();
  void reset_graph_state();
  void tick(const Player& player, Stack& allocator);
  void depth_first_cancel(const Connection& connection);
  void depth_first_cancel(uint32_t entity);

private:
  //
  // Returns:
  // true  - node still active
  // false - node finished executing
  //
  bool update(const Player& player, uint32_t entity_idx);
};

} // namespace story
