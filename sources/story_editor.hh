#pragma once

#include "color_palette.hh"
#include "engine/hierarchical_allocator.hh"
#include "engine/math.hh"
#include "story.hh"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_stdinc.h>

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
  bool                   is_point_requested_to_render          = false;
  Vec3                   point_to_render                       = {};

  void setup(HierarchicalAllocator& allocator);
  void teardown(HierarchicalAllocator& allocator);
  void load(SDL_RWops* handle);
  void save(SDL_RWops* handle);
  void tick(const Player& player, Stack& allocator);
  void imgui_update();
  void editor_update(const SDL_Event& event);
  void render_node_edit_window(const Player& player);

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

} // namespace story
