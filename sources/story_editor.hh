#pragma once

#include "color_palette.hh"
#include "engine/hierarchical_allocator.hh"
#include "engine/math.hh"
#include "story.hh"

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_stdinc.h>

struct Stack;

namespace story {

struct ClickedPositionTracker
{
  bool state         = false;
  Vec2 origin        = {};
  Vec2 offset        = {};
  Vec2 last_position = {};

  explicit operator bool() const
  {
    return state;
  }

  void activate(const Vec2& position);
  void deactivate();
  void update(const Vec2& position);
};

struct StoryEditor : protected Story
{
  ClickedPositionTracker lmb                                   = {};
  ClickedPositionTracker rmb                                   = {};
  ClickedPositionTracker mmb                                   = {};
  bool                   element_clicked                       = false;
  bool                   connection_building_active            = false;
  bool                   connection_building_input_clicked     = false;
  uint32_t               connection_building_idx_clicked_first = 0u;
  uint32_t               connection_building_dot_idx           = 0u;
  bool                   selection_box_active                  = false;
  Vec2                   selection_box_ul                      = {};
  Vec2                   selection_box_br                      = {};
  float                  zoom                                  = 0.0f;
  Vec2                   blackboard_origin_offset              = {};
  Vec2*                  positions                             = nullptr;
  Vec2*                  positions_before_grab_movement        = nullptr;
  uint8_t*               is_selected                           = nullptr;
  bool                   is_shift_pressed                      = false;
  bool                   is_showing_state                      = false;
  Palette                palette_default                       = {};
  Palette                palette_debugger                      = {};

  void setup(HierarchicalAllocator& allocator);
  void teardown(HierarchicalAllocator& allocator);
  void load(SDL_RWops* handle);
  void save(SDL_RWops* handle);
  void tick(Stack& allocator);
  void imgui_update();
  void editor_update(const SDL_Event& event);

private:
  [[nodiscard]] const Palette& get_palette() const;
  void                         handle_mouse_wheel(float val);
  void                         handle_mouse_motion(const Vec2& motion);
  void                         select_element_at_position(const Vec2& position);
  void                         recalculate_selection_box();
  [[nodiscard]] bool           is_any_selected(uint32_t count) const;
  [[nodiscard]] bool           is_selection_box_active() const;
  [[nodiscard]] Vec2           calc_blackboard_offset() const;
  void                         remove_selected_nodes();
};

struct Data;

//
// Development only data
//
struct EditorData
{
  ClickedPositionTracker lmb = {};
  ClickedPositionTracker rmb = {};
  ClickedPositionTracker mmb = {};

  bool element_clicked = false;

  bool     connection_building_active            = false;
  bool     connection_building_input_clicked     = false;
  uint32_t connection_building_idx_clicked_first = 0u;
  uint32_t connection_building_dot_idx           = 0u;

  bool selection_box_active = false;
  Vec2 selection_box_ul     = {};
  Vec2 selection_box_br     = {};

  float    zoom                           = 0.0f;
  Vec2     blackboard_origin_offset       = {};
  Vec2*    positions                      = nullptr;
  Vec2*    positions_before_grab_movement = nullptr;
  uint8_t* is_selected                    = nullptr;
  bool     is_shift_pressed               = false;
  bool     is_showing_state               = false;

  Palette palette_default  = {};
  Palette palette_debugger = {};

  const Palette& get_palette()
  {
    return is_showing_state ? palette_debugger : palette_default;
  }

  void handle_mouse_wheel(float val);
  void handle_mouse_motion(const Data& data, const Vec2& motion);
  void select_element_at_position(const Vec2& position, Data& data);
  void recalculate_selection_box();

  [[nodiscard]] bool is_any_selected(uint32_t count) const;

  [[nodiscard]] bool is_selection_box_active() const
  {
    return selection_box_active and (not element_clicked);
  }

  [[nodiscard]] Vec2 calc_blackboard_offset() const
  {
    return blackboard_origin_offset + mmb.offset;
  };

  void remove_selected_nodes(Data& data);
};

//
// Binary script file format
//
// [uint32_t] entities count
// ... nodes[]
// ... editor_data.positions[]
// [uint32_t] target positions count
// ... target_positions[]
// [uint32_t] connections count
// ... connections[]
//
// Q: Why aren't node_states serialized as well?
// A: We want to game start from clean state to avoid any logic / world errors
//    This means that only the first "Start" node will be set to "Active" at the start of the game.
//

struct Data
{
  //
  // ENTITIES
  // index in tables below indicates the entity number
  //
  Node*    nodes        = nullptr;
  State*   node_states  = nullptr;
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
  void save_to_handle(SDL_RWops* handle);
  void push_connection(const Connection& conn);
  void dump_connections() const;
  void reset_graph_state();
};

void tick(Stack& allocator, Data& data);

} // namespace story
