#include "story_editor.hh"
#include "color_palette.hh"
#include "engine/fileops.hh"
#include "imgui.h"
#include "player.hh"
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_assert.h>
#include <algorithm>

namespace story {

namespace {

constexpr float offset_from_top          = 25.0f;
constexpr float dot_size                 = 5.0f;
const char*     default_script_file_name = "default_story_script.bin";

struct NodeBox
{
  const char* name = "";
  Vec2        size;
  uint32_t    inputs_count  = 0;
  uint32_t    outputs_count = 0;
};

constexpr NodeBox StartBox = {
    .name          = "Start",
    .size          = Vec2(120.0f, 80.0f),
    .inputs_count  = 0,
    .outputs_count = 1,
};

constexpr NodeBox GoToBox = {
    .name          = "GoTo",
    .size          = Vec2(120.0f, 80.0f),
    .inputs_count  = 1,
    .outputs_count = 1,
};

constexpr NodeBox DialogueBox = {
    .name          = "Dialogue",
    .size          = Vec2(120.0f, 80.0f),
    .inputs_count  = 1,
    .outputs_count = 1,
};

constexpr NodeBox AllBox = {
    .name          = "All",
    .size          = Vec2(120.0f, 80.0f),
    .inputs_count  = 1,
    .outputs_count = 1,
};

constexpr NodeBox AnyBox = {
    .name          = "Any",
    .size          = Vec2(120.0f, 80.0f),
    .inputs_count  = 1,
    .outputs_count = 1,
};

constexpr const NodeBox& select(Node type)
{
  switch (type)
  {
  default:
  case Node::Start:
    return StartBox;
  case Node::Any:
    return AnyBox;
  case Node::All:
    return AllBox;
  case Node::GoTo:
    return GoToBox;
  case Node::Dialogue:
    return DialogueBox;
  }
}

struct NodeDescription
{
  Node type     = Node::Start;
  Vec2 position = Vec2();
};

void add_vertical_line(ImDrawList* draw_list, float x, float y_bottom, float length, ImU32 color)
{
  draw_list->AddLine(ImVec2(x, y_bottom), ImVec2(x, y_bottom + length), color);
}

void add_horizontal_line(ImDrawList* draw_list, float x_left, float y, float length, ImU32 color)
{
  draw_list->AddLine(ImVec2(x_left, y), ImVec2(x_left + length, y), color);
}

bool is_point_enclosed(const Vec2& ul, const Vec2& br, const Vec2& pt)
{
  return (ul.x <= pt.x) && (br.x >= pt.x) && (ul.y <= pt.y) && (br.y >= pt.y);
}

Vec2 to_vec2(const SDL_MouseMotionEvent& event)
{
  return Vec2(static_cast<float>(event.x), static_cast<float>(event.y));
}

Vec2 get_mouse_state()
{
  int x = 0;
  int y = 0;
  SDL_GetMouseState(&x, &y);
  return Vec2(static_cast<float>(x), static_cast<float>(y));
}

} // namespace

struct ScaledBox
{
  ScaledBox(const Vec2& position, const Vec2& size, float zoom, const Vec2& origin)
      : up(offset_from_top + ((origin.y + position.y) * zoom))
      , bottom(up + (size.y * zoom))
      , left((origin.x + position.x) * zoom)
      , right(left + (size.x * zoom))
      , zoom(zoom)
  {
  }

  [[nodiscard]] ImVec2 calculate_output_dot_position(const NodeBox& render_params, uint32_t i) const
  {
    const uint32_t output_splits_count = render_params.outputs_count + 1;
    const float    output_y_offset     = (render_params.size.y * zoom) / output_splits_count;
    const float    output_x_position   = right - (8.0f * zoom);

    return {output_x_position, up + (static_cast<float>(i + 1) * output_y_offset)};
  }

  [[nodiscard]] ImVec2 calculate_input_dot_position(const NodeBox& render_params, uint32_t i) const
  {
    const uint32_t input_splits_count = render_params.inputs_count + 1;
    const float    input_y_offset     = (render_params.size.y * zoom) / input_splits_count;
    const float    input_x_position   = left + (8.0f * zoom);

    return {input_x_position, up + (static_cast<float>(i + 1) * input_y_offset)};
  }

  float up;
  float bottom;
  float left;
  float right;
  float zoom;
};

void draw_connection_bezier(ImDrawList* draw_list, const ImVec2& from, const ImVec2& to,
                            ImColor color = ImColor(255, 255, 255, 120))
{
  const ImVec2 cp0 = {to.x - 0.5f * (to.x - from.x), from.y + 0.15f * (to.y - from.y)};
  const ImVec2 cp1 = {from.x + 0.5f * (to.x - from.x), to.y - 0.15f * (to.y - from.y)};
  draw_list->AddBezierCurve(from, cp0, cp1, to, color, 5.0f);
}

ImColor rainbow_color(float time)
{
  return ImColor(SDL_fabsf(SDL_sinf(time)), SDL_fabsf(SDL_cosf(time)), SDL_fabsf(SDL_cosf(1.5f * time)));
}

ImColor rainbow_color()
{
  return rainbow_color(2.5f * static_cast<float>(ImGui::GetTime()));
}

ImVec2 to_imvec2(const Vec2& in)
{
  return ImVec2(in.x, in.y);
}

void draw_selection_box(ImDrawList* draw_list, const Vec2& ul, const Vec2& br)
{
  const ImVec2  im_ul    = to_imvec2(ul);
  const ImVec2  im_br    = to_imvec2(br);
  const ImColor bg_color = ImColor(0.3f, 0.3f, 1.0f, 0.1f);

  draw_list->AddRectFilled(im_ul, im_br, bg_color, 3.0f, ImDrawCornerFlags_All);
  draw_list->AddRect(im_ul, im_br, rainbow_color(), 3.0f, ImDrawCornerFlags_All, 0.5f);
}

static bool is_intersecting(const Vec2& a_ul, const Vec2& a_br, const Vec2& b_ul, const Vec2& b_br)
{
  return (a_ul.x < b_br.x) and (a_br.x > b_ul.x) and (a_ul.y < b_br.y) and (a_br.y > b_ul.y);
}

const char* state_to_string(State state)
{
  switch (state)
  {
  case State::Upcoming:
    return "Upcoming";
  case State::Active:
    return "Active";
  case State::Finished:
    return "Finished";
  case State::Cancelled:
    return "Cancelled";
  default:
    return "N/A";
  }
}

static ImColor to_imgui(const Palette::RGB& rgb, int alpha)
{
  return {rgb.r, rgb.g, rgb.b, alpha};
}

void ClickedPositionTracker::activate(const Vec2& position)
{
  state  = true;
  origin = position;
}

void ClickedPositionTracker::deactivate()
{
  state         = false;
  last_position = origin + offset;
  offset        = Vec2();
  origin        = Vec2();
}

void ClickedPositionTracker::update(const Vec2& position)
{
  offset = position - origin;
}

//////////////////////////////////////////////////////////
//                 NEW IMPLEMENTATION
//////////////////////////////////////////////////////////

void StoryEditor::setup(MemoryAllocator& allocator)
{
  Story::setup(allocator);

  positions                      = reinterpret_cast<Vec2*>(allocator.Allocate(sizeof(Vec2) * entities_capacity));
  positions_before_grab_movement = reinterpret_cast<Vec2*>(allocator.Allocate(sizeof(Vec2) * entities_capacity));
  is_selected                    = reinterpret_cast<uint8_t*>(allocator.Allocate(sizeof(uint8_t) * entities_capacity));

  std::fill(is_selected, is_selected + entities_capacity, SDL_FALSE);

  SDL_RWops* rw = SDL_RWFromFile(default_script_file_name, "rb");
  // SDL_RWops* rw = nullptr;
  if (rw)
  {
    const uint32_t size = static_cast<uint32_t>(SDL_RWsize(rw));
    SDL_Log("\"%s\" found (%u bytes) Loading game from external source", default_script_file_name, size);
    load(rw);
    validate_and_fix();
    SDL_RWclose(rw);
  }
  else
  {
    SDL_Log("\"%s\" not found. Using built-in", default_script_file_name);

    constexpr NodeDescription initial_nodes[] = {
        {
            Node::Start,
            Vec2(50.0f, 10.0f),
        },
        {
            Node::Any,
            Vec2(200.0f, 40.0f),
        },
        {
            Node::All,
            Vec2(400.0f, 40.0f),
        },
    };

    entity_count      = SDL_arraysize(initial_nodes);
    auto get_type     = [](const NodeDescription& d) { return d.type; };
    auto get_position = [](const NodeDescription& d) { return d.position; };
    std::transform(initial_nodes, initial_nodes + entity_count, nodes, get_type);
    std::transform(initial_nodes, initial_nodes + entity_count, positions, get_position);

    constexpr Connection test_connections[] = {
        {
            .src_node_idx   = 0,
            .src_output_idx = 0,
            .dst_input_idx  = 0,
            .dst_node_idx   = 1,
        },
    };

    connections_count = SDL_arraysize(test_connections);
    std::copy(test_connections, test_connections + connections_count, connections);
  }

  zoom             = 1.0f;
  node_states[0]   = State::Active;
  palette_default  = Palette::generate_happyhue_13();
  palette_debugger = Palette::generate_happyhue_3();

  is_showing_state = true;
}

void StoryEditor::teardown()
{
  allocator->Free(positions, sizeof(Vec2) * entities_capacity);
  allocator->Free(positions_before_grab_movement, sizeof(Vec2) * entities_capacity);
  allocator->Free(is_selected, sizeof(uint8_t) * entities_capacity);
  Story::teardown();
}

void StoryEditor::load(SDL_RWops* handle)
{
  Story::load(handle);

  FileOps s(handle);
  s.deserialize(positions, entity_count);
  std::copy(positions, positions + entity_count, positions_before_grab_movement);
  std::fill(is_selected, is_selected + entity_count, SDL_FALSE);
  reset_graph_state();
}

void StoryEditor::save(SDL_RWops* handle)
{
  Story::save(handle);

  FileOps s(handle);
  s.serialize(positions, entity_count);
}

void StoryEditor::tick(const Player& player, MemoryAllocator& allocator)
{
  Story::tick(player, allocator);
}

void StoryEditor::imgui_update()
{
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  //////////////////////////////////////////////////////////////////////////////
  //
  // Background grid rendering
  //
  //////////////////////////////////////////////////////////////////////////////
  {
    ImVec2         size                   = ImGui::GetWindowSize();
    ImVec2         position               = ImGui::GetWindowPos();
    ImVec2         offset                 = to_imvec2(calc_blackboard_offset().scale(zoom));
    float          grid                   = 32.0f * zoom;
    ImColor        grid_line_color        = to_imgui(palette_default.background, 80);
    const uint32_t vertical_lines_count   = size.x / grid;
    const uint32_t horizontal_lines_count = size.y / grid;

    position.y += offset_from_top;

    for (uint32_t i = 0; i < vertical_lines_count; ++i)
    {
      float x = SDL_fmodf(offset.x + grid * i, grid * vertical_lines_count);
      while (x < 0.0f)
      {
        x += grid * vertical_lines_count;
      }
      add_vertical_line(draw_list, x + position.x, position.y, size.y, grid_line_color);
    }

    for (uint32_t i = 0; i < horizontal_lines_count; ++i)
    {
      float y = SDL_fmodf(offset.y + grid * i, grid * horizontal_lines_count);
      while (y < 0.0f)
      {
        y += grid * horizontal_lines_count;
      }
      add_horizontal_line(draw_list, position.x, y + position.y, size.x, grid_line_color);
    }

    const ImVec2 center_pos = {position.x + offset.x, position.y + offset.y};
    draw_list->AddCircleFilled(center_pos, 5.0, ImColor(0.1f, 0.2f, 0.6f, 1.0f));
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Calculating which elements are selected by selection box
  //
  //////////////////////////////////////////////////////////////////////////////
  if (is_selection_box_active())
  {
    recalculate_selection_box();
    std::fill(is_selected, is_selected + entity_count, SDL_FALSE);

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      const NodeBox&  render_params = select(nodes[i]);
      const ScaledBox box           = ScaledBox(positions[i], render_params.size, zoom, calc_blackboard_offset());

      const Vec2 ul = Vec2(box.left, box.up);
      const Vec2 br = Vec2(box.right, box.bottom);

      if (is_intersecting(selection_box_ul, selection_box_br, ul, br))
      {
        is_selected[i] = SDL_TRUE;
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Precaching color values
  //
  //////////////////////////////////////////////////////////////////////////////

  const ImColor color_upcoming  = to_imgui(palette_debugger.paragraph, 170);
  const ImColor color_active    = to_imgui(palette_debugger.tertiary, 170);
  const ImColor color_finished  = to_imgui(palette_debugger.button, 170);
  const ImColor color_cancelled = to_imgui(palette_default.background, 170);
  const ImColor color_regular   = to_imgui(palette_default.secondary, 170);
  const ImColor color_special   = to_imgui(palette_default.tertiary, 170);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Story node rendering
  //
  //////////////////////////////////////////////////////////////////////////////
  for (uint32_t i = 0; i < entity_count; ++i)
  {
    const NodeBox&  render_params = select(nodes[i]);
    const ScaledBox box           = ScaledBox(positions[i], render_params.size, zoom, calc_blackboard_offset());
    const ImVec2    ul            = ImVec2(box.left, box.up);
    const ImVec2    br            = ImVec2(box.right, box.bottom);

    // debug node parameters
    if (is_selected[i])
    {
      const Vec2& position = positions[i];
      ImGui::Text("selected: %s entity index: %u position: [%.2f %.2f] state: %s", render_params.name, i, position.x,
                  position.y, state_to_string(node_states[i]));
    }

    // @TODO draw only if on screen

    if (is_showing_state)
    {
      const ImColor* selected_color = &color_upcoming;
      switch (node_states[i])
      {
      case State::Upcoming:
        selected_color = &color_upcoming;
        break;
      case State::Active:
        selected_color = &color_active;
        break;
      case State::Finished:
        selected_color = &color_finished;
        break;
      case State::Cancelled:
        selected_color = &color_cancelled;
        break;
      }
      draw_list->AddRectFilled(ul, br, *selected_color, 5.0f);
    }
    else
    {
      const ImColor* selected_color = &color_upcoming;
      switch (nodes[i])
      {
      case Node::GoTo:
        selected_color = &color_special;
        break;
      default:
        selected_color = &color_regular;
        break;
      }
      draw_list->AddRectFilled(ul, br, *selected_color, 5.0f);
    }

    if (is_selected[i])
    {
      draw_list->AddRect(ul, br, rainbow_color(), 5.0f, ImDrawCornerFlags_All, 4.0f);
    }
    else
    {
      draw_list->AddRect(ul, br, ImColor(0, 0, 0, 210), 5.0f, ImDrawCornerFlags_All, 2.0f);
    }

    if (0.3f < zoom)
    {
      const ImVec2 ul_text = ImVec2(ul.x + 5.0f, ul.y + 5.0f);
      draw_list->AddText(ul_text, to_imgui(get_palette().button_text, 255), render_params.name);
    }

    for (uint32_t i = 0; i < render_params.inputs_count; ++i)
    {
      draw_list->AddCircleFilled(box.calculate_input_dot_position(render_params, i), dot_size * zoom,
                                 to_imgui(get_palette().paragraph, 200));
    }

    for (uint32_t i = 0; i < render_params.outputs_count; ++i)
    {
      draw_list->AddCircleFilled(box.calculate_output_dot_position(render_params, i), dot_size * zoom,
                                 to_imgui(get_palette().paragraph, 200));
    }
  }

  for (uint32_t i = 0; i < connections_count; ++i)
  {
    const Connection& connection = connections[i];

    const NodeBox& src_render_params = select(nodes[connection.src_node_idx]);
    const NodeBox& dst_render_params = select(nodes[connection.dst_node_idx]);

    const ScaledBox src_box(positions[connection.src_node_idx], src_render_params.size, zoom, calc_blackboard_offset());
    const ScaledBox dst_box(positions[connection.dst_node_idx], dst_render_params.size, zoom, calc_blackboard_offset());

    draw_connection_bezier(draw_list,
                           src_box.calculate_output_dot_position(src_render_params, connection.src_output_idx),
                           dst_box.calculate_input_dot_position(dst_render_params, connection.dst_input_idx),
                           to_imgui(get_palette().paragraph, 180));
  }

  if (connection_building_active)
  {
    const Vec2      mouse         = get_mouse_state();
    const NodeBox&  render_params = select(nodes[element_clicked]);
    const ScaledBox src_box =
        ScaledBox(positions[connection_building_idx_clicked_first], render_params.size, zoom, calc_blackboard_offset());

    const ImVec2 src_point = connection_building_input_clicked
                                 ? src_box.calculate_input_dot_position(render_params, connection_building_dot_idx)
                                 : src_box.calculate_output_dot_position(render_params, connection_building_dot_idx);

    draw_connection_bezier(draw_list, src_point, ImVec2(mouse.x, mouse.y), rainbow_color());
  }

  if (ImGui::BeginPopupContextWindow())
  {
    if (ImGui::BeginMenu("New node"))
    {
      struct Combination
      {
        story::Node type;
        const char* name;
      };

      const Combination combinations[] = {
          {Node::Any, "Any"},
          {Node::All, "All"},
          {Node::GoTo, "GoTo"},
          {Node::Dialogue, "Dialogue"},
      };

      for (const Combination& combination : combinations)
      {
        if (ImGui::MenuItem(combination.name))
        {
          const uint32_t node_idx = entity_count++;
          nodes[node_idx]         = combination.type;

          const Vec2 position = rmb.last_position.scale(1.0f / zoom) - calc_blackboard_offset();

          positions[node_idx]                      = position;
          positions_before_grab_movement[node_idx] = position;

          if (Node::GoTo == combination.type)
          {
            TargetPosition co = {
                .entity   = node_idx,
                .position = Vec3(),
                .radius   = 1.0f,
            };
            target_positions[target_positions_count++] = co;
          }
          else if (Node::Dialogue == combination.type)
          {
            Dialogue co = {
                .entity = node_idx,
                .type   = Dialogue::Type::Short,
                .text   = reinterpret_cast<char*>(
                    allocator->Allocate(sizeof(char) * Dialogue::type_to_size(Dialogue::Type::Short))),
            };
            dialogues[dialogues_count++] = co;
          }
        }
      }
      ImGui::EndMenu();
    }

    {
      auto to_state_string = [](bool state) { return state ? "Disable debugger" : "Enable debugger"; };
      if (ImGui::MenuItem(to_state_string(is_showing_state)))
      {
        is_showing_state = !is_showing_state;
      }
    }

    if (ImGui::MenuItem("Reset view"))
    {
      zoom                     = 1.0f;
      blackboard_origin_offset = Vec2(0.0f, 0.0f);
    }

    if (ImGui::MenuItem("Reset graph state"))
    {
      reset_graph_state();
    }

    if (ImGui::BeginMenu("Etc"))
    {
      if (ImGui::MenuItem("Load default"))
      {
        SDL_RWops* handle = SDL_RWFromFile(default_script_file_name, "rb");
        load(handle);
        SDL_RWclose(handle);
        SDL_Log("Loaded file %s", default_script_file_name);
      }

      if (ImGui::MenuItem("Save default"))
      {
        SDL_RWops* handle = SDL_RWFromFile(default_script_file_name, "wb");
        save(handle);
        SDL_RWclose(handle);
        SDL_Log("Saved file %s", default_script_file_name);
      }

      ImGui::EndMenu();
    }

    ImGui::EndPopup();
  }

  if (is_selection_box_active())
  {
    draw_selection_box(draw_list, selection_box_ul, selection_box_br);
  }
}

void StoryEditor::editor_update(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_MOUSEWHEEL: {
    if ((not mmb.state) and (0 != event.wheel.y))
    {
      handle_mouse_wheel((0 > event.wheel.y) ? -0.05f : 0.05f);
    }
  }
  break;
  case SDL_MOUSEMOTION: {
    handle_mouse_motion(to_vec2(event.motion));
  }
  break;
  case SDL_MOUSEBUTTONDOWN: {
    switch (event.button.button)
    {
    case SDL_BUTTON_LEFT:
      lmb.activate(get_mouse_state());
      select_element_at_position(lmb.origin);
      break;

    case SDL_BUTTON_RIGHT:
      rmb.activate(get_mouse_state());
      break;

    case SDL_BUTTON_MIDDLE:
      mmb.activate(get_mouse_state().scale(1.0f / zoom));
      break;

    default:
      break;
    }
  }
  break;
  case SDL_MOUSEBUTTONUP: {
    switch (event.button.button)
    {
    case SDL_BUTTON_LEFT:
      lmb.deactivate();
      if (selection_box_active)
      {
        for (uint32_t i = 0; i < entity_count; ++i)
        {
          if (is_selected[i])
          {
            positions_before_grab_movement[i] = positions[i];
          }
        }
      }
      selection_box_active = false;
      break;

    case SDL_BUTTON_RIGHT:
      rmb.deactivate();
      break;

    case SDL_BUTTON_MIDDLE:
      blackboard_origin_offset += mmb.offset;
      mmb.deactivate();
      break;

    default:
      break;
    }
  }
  break;
  case SDL_KEYDOWN:
    if (SDL_SCANCODE_LSHIFT == event.key.keysym.scancode)
    {
      is_shift_pressed = true;
    }
    else if (SDL_SCANCODE_X == event.key.keysym.scancode)
    {
      if ((not is_selection_box_active()) and is_any_selected(entity_count))
      {
        remove_selected_nodes();
      }
    }
    break;
  case SDL_KEYUP:
    if (SDL_SCANCODE_LSHIFT == event.key.keysym.scancode)
    {
      is_shift_pressed = false;
    }
    break;
  }
}

const Palette& StoryEditor::get_palette() const
{
  return is_showing_state ? palette_debugger : palette_default;
}

void StoryEditor::handle_mouse_wheel(float val)
{
  zoom = clamp(zoom + val, 0.1f, 10.0f);
}

void StoryEditor::handle_mouse_motion(const Vec2& motion)
{
  if (lmb)
  {
    lmb.update(motion);

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      if (is_selected[i] and (not is_selection_box_active()))
      {
        positions[i] = positions_before_grab_movement[i] + lmb.offset.scale(1.0f / zoom);
      }
    }
  }
  else if (mmb)
  {
    mmb.update(motion.scale(1.0f / zoom));
  }
}

void StoryEditor::select_element_at_position(const Vec2& position)
{
  element_clicked = false;
  for (uint32_t node_idx = 0; node_idx < entity_count; ++node_idx)
  {
    const uint32_t  reverse_node_idx = entity_count - node_idx - 1;
    const NodeBox&  render_params    = select(nodes[reverse_node_idx]);
    const ScaledBox box = ScaledBox(positions[reverse_node_idx], render_params.size, zoom, calc_blackboard_offset());
    const Vec2      ul  = Vec2(box.left, box.up);
    const Vec2      br  = Vec2(box.right, box.bottom);

    if (is_point_enclosed(ul, br, lmb.origin))
    {
      element_clicked = true;

      if (SDL_FALSE == is_selected[reverse_node_idx])
      {
        std::fill(is_selected, is_selected + entity_count, SDL_FALSE);
        is_selected[reverse_node_idx] = SDL_TRUE;
      }

      for (uint32_t i = 0; i < render_params.inputs_count; ++i)
      {
        const ImVec2 dot_position = box.calculate_input_dot_position(render_params, i);
        const Vec2   distance     = position - Vec2(dot_position.x, dot_position.y);
        if (distance.len() < (dot_size * zoom))
        {
          if (connection_building_active and (connection_building_idx_clicked_first != reverse_node_idx))
          {
            SDL_Log("[connection building - END] %d input clicked! (%s)", i, render_params.name);
            if (connection_building_input_clicked)
            {
              SDL_Log("[ERR] Can't connect input with input!");
            }
            else
            {
              const Connection new_connection = {
                  .src_node_idx   = connection_building_idx_clicked_first,
                  .src_output_idx = connection_building_dot_idx,
                  .dst_input_idx  = i,
                  .dst_node_idx   = reverse_node_idx,
              };

              push_connection(new_connection);
            }
            connection_building_active = false;
          }
          else
          {
            SDL_Log("[connection building - START] %d input clicked! (%s)", i, render_params.name);
            connection_building_active            = true;
            connection_building_input_clicked     = true;
            connection_building_idx_clicked_first = reverse_node_idx;
            connection_building_dot_idx           = i;
          }
          return;
        }
      }

      for (uint32_t i = 0; i < render_params.outputs_count; ++i)
      {
        const ImVec2 dot_position = box.calculate_output_dot_position(render_params, i);
        const Vec2   distance     = position - Vec2(dot_position.x, dot_position.y);
        if (distance.len() < (dot_size * zoom))
        {
          if (connection_building_active and (connection_building_idx_clicked_first != reverse_node_idx))
          {
            SDL_Log("[connection building - END] %d output clicked! (%s)", i, render_params.name);
            if (!connection_building_input_clicked)
            {
              SDL_Log("[ERR] Can't connect output with output!");
            }
            else
            {
              const Connection new_connection = {
                  .src_node_idx   = reverse_node_idx,
                  .src_output_idx = i,
                  .dst_input_idx  = connection_building_dot_idx,
                  .dst_node_idx   = connection_building_idx_clicked_first,
              };

              push_connection(new_connection);
            }
            connection_building_active = false;
          }
          else
          {
            SDL_Log("[connection building - START] %d output clicked! (%s)", i, render_params.name);
            connection_building_active            = true;
            connection_building_input_clicked     = false;
            connection_building_idx_clicked_first = reverse_node_idx;
            connection_building_dot_idx           = i;
          }

          return;
        }
      }

      break;
    }
  }

  if (not element_clicked)
  {
    std::fill(is_selected, is_selected + entity_count, SDL_FALSE);
    connection_building_active = false;
  }

  selection_box_active = true;
}

void StoryEditor::recalculate_selection_box()
{
  const Vec2 start = lmb.origin;
  const Vec2 end   = start + lmb.offset;

  if (start.x < end.x)
  {
    if (start.y < end.y)
    {
      // start
      //        *
      //            end
      selection_box_ul = Vec2(start.x, start.y);
      selection_box_br = Vec2(end.x, end.y);
    }
    else
    {
      //            end
      //        *
      // start
      selection_box_ul = Vec2(start.x, end.y);
      selection_box_br = Vec2(end.x, start.y);
    }
  }
  else
  {
    if (start.y > end.y)
    {
      // end
      //        *
      //            start
      selection_box_ul = Vec2(end.x, end.y);
      selection_box_br = Vec2(start.x, start.y);
    }
    else
    {
      //            start
      //        *
      // end
      selection_box_ul = Vec2(end.x, start.y);
      selection_box_br = Vec2(start.x, end.y);
    }
  }
}

bool StoryEditor::is_any_selected(uint32_t count) const
{
  uint8_t* end = &is_selected[count];
  return end != std::find(is_selected, end, SDL_TRUE);
}

bool StoryEditor::is_selection_box_active() const
{
  return selection_box_active and (not element_clicked);
}

Vec2 StoryEditor::calc_blackboard_offset() const
{
  return blackboard_origin_offset + mmb.offset;
}

void StoryEditor::remove_selected_nodes()
{
  {
    uint32_t removed_entities = 0;
    for (uint8_t* it = std::find(is_selected, &is_selected[entity_count], SDL_TRUE); it != &is_selected[entity_count];
         it          = std::find(it + 1, &is_selected[entity_count], SDL_TRUE))
    {
      const uint32_t entity_idx = std::distance(is_selected, it) - removed_entities;

      Connection* new_connections_end =
          std::remove_if(connections, &connections[connections_count], [entity_idx](const Connection& c) {
            return (entity_idx == c.src_node_idx) or (entity_idx == c.dst_node_idx);
          });

      std::for_each(connections, new_connections_end, [entity_idx](Connection& c) {
        if (c.src_node_idx > entity_idx)
        {
          c.src_node_idx -= 1;
        }
        if (c.dst_node_idx > entity_idx)
        {
          c.dst_node_idx -= 1;
        }
      });

      connections_count = std::distance(connections, new_connections_end);
      removed_entities += 1;
    }
  }

  for (uint8_t* it = std::find(is_selected, &is_selected[entity_count], SDL_TRUE); it != &is_selected[entity_count];
       it          = std::find(it, &is_selected[entity_count], SDL_TRUE))
  {
    const uint32_t entity_idx = std::distance(is_selected, it);

    auto remove_element = [entity_idx](auto* array, uint32_t end_offset) {
      std::rotate(array + entity_idx, array + entity_idx + 1, &array[end_offset]);
    };

    remove_element(positions, entity_count);
    remove_element(positions_before_grab_movement, entity_count);
    remove_element(is_selected, entity_count);
    remove_element(nodes, entity_count);
    remove_element(node_states, entity_count);

    for (uint32_t i = 0; i < target_positions_count; ++i)
    {
      TargetPosition& co = target_positions[i];
      if (entity_idx <= co.entity)
      {
        co.entity -= 1;
      }
    }

    for (uint32_t i = 0; i < dialogues_count; ++i)
    {
      Dialogue& co = dialogues[i];
      if (entity_idx <= co.entity)
      {
        co.entity -= 1;
      }
    }

    entity_count -= 1;
  }

  std::fill(is_selected, is_selected + entity_count, SDL_FALSE);
}

void StoryEditor::render_node_edit_window(const Player& player)
{
  is_point_requested_to_render = false;

  //
  // Helper editor window will only show for singular selections
  //
  uint32_t n = std::count(is_selected, is_selected + entity_count, SDL_TRUE);
  if (1 == n)
  {
    const uint32_t entity = std::distance(is_selected, std::find(is_selected, is_selected + entity_count, SDL_TRUE));
    if (Node::GoTo == nodes[entity])
    {
      is_point_requested_to_render = true;
      if (ImGui::Begin("GoTo Inspector", nullptr,
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus))
      {
        ImGui::Text("Entity %u", entity);

        TargetPosition* co = std::find(target_positions, target_positions + target_positions_count, entity);
        SDL_assert((target_positions + target_positions_count) != co);

        point_to_render = co->position;

        ImGui::DragFloat3("Target Position", &co->position.x);
        ImGui::InputFloat("Radius", &co->radius);
        ImGui::Text("Distance from player: %.3f", (player.position - co->position).len());
        ImGui::Text("State: ");
        ImGui::SameLine();

        const State state = node_states[entity];
        ImVec4      color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

        if (State::Cancelled == state)
        {
          color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        else if (State::Upcoming == state)
        {
          color = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        }

        ImGui::TextColored(color, "%s", state_to_string(state));

        ImGui::End();
      }
    }
    else if (Node::Dialogue == nodes[entity])
    {
      if (ImGui::Begin("Dialogue Inspector", nullptr,
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus))
      {
        ImGui::Text("Entity %u", entity);

        Dialogue* co = std::find(dialogues, dialogues + dialogues_count, entity);
        SDL_assert((dialogues + dialogues_count) != co);

        ImGui::InputTextMultiline("text", co->text, Dialogue::type_to_size(co->type));
        ImGui::Text("State: ");
        ImGui::SameLine();

        const State state = node_states[entity];
        ImVec4      color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

        if (State::Cancelled == state)
        {
          color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        else if (State::Upcoming == state)
        {
          color = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        }

        ImGui::TextColored(color, "%s", state_to_string(state));

        ImGui::End();
      }
    }
  }
}

} // namespace story
