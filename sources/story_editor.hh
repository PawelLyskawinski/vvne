#pragma once

#include "engine/hierarchical_allocator.hh"
#include "engine/math.hh"
#include "story_components.hh"
#include <SDL2/SDL_events.h>

namespace story {

struct EditorData
{
  bool     lmb_clicked                       = false;
  Vec2     lmb_clicked_position              = {};
  Vec2     lmb_clicked_offset                = {};
  bool     element_clicked                   = false;
  uint32_t element_clicked_idx               = 0u;
  Vec2     element_clicked_original_position = {};
  float    zoom                              = 0.0f;
  Vec2     blackboard_origin_offset          = {};
  Vec2*    positions                         = nullptr;

  void handle_mouse_wheel(float val);
  void handle_mouse_motion(const Vec2& motion);
  void handle_mouse_lmb_down(const Vec2& position, uint32_t nodes_count, const Node* nodes);
  void handle_mouse_lmb_up();
};

//
// Binary script file format
//
// [uint32_t] entities count
// ... nodes[]
// ... node_states[]
// ... editor_data.positions[]
// [uint32_t] target positions count
// ... target_positions[]
// [uint32_t] connections count
// ... connections[]
//

struct Data
{
  //
  // ENTITIES
  // index in tables below indicates the entity number
  //
  Node*    nodes         = nullptr;
  State*   node_states   = nullptr;
  uint32_t entity_count = 0;

  //
  // COMPONENTS
  // Component data structures store entity numbers associated with them
  //
  TargetPosition* target_positions       = nullptr;
  uint32_t        target_positions_count = 0;

  Connection* connections       = nullptr;
  uint32_t    connections_count = 0;

  //
  // Additional editor related features
  //
  EditorData editor_data = {};

  void init(HierarchicalAllocator& allocator);
  void imgui_update();
  void editor_update(const SDL_Event& event);
  void load_from_handle(SDL_RWops* handle);
};

} // namespace story
