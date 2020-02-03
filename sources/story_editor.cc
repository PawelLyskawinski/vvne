#include "story_editor.hh"
#include "color_palette.hh"
#include "imgui.h"
#include <SDL2/SDL_log.h>
#include <algorithm>

namespace story {

namespace {

constexpr uint32_t entities_capacity        = 256;
constexpr uint32_t connections_capacity     = 10'240;
constexpr uint32_t components_capacity      = 64;
constexpr float    offset_from_top          = 25.0f;
constexpr float    dot_size                 = 5.0f;
const char*        default_script_file_name = "default_story_script.bin";

struct NodeBox
{
  const char* name = "";
  Vec2        size;
  uint32_t    inputs_count;
  uint32_t    outputs_count;
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

void Data::init(HierarchicalAllocator& allocator)
{
  nodes                                      = allocator.allocate<Node>(entities_capacity);
  node_states                                = allocator.allocate<State>(entities_capacity);
  target_positions                           = allocator.allocate<TargetPosition>(components_capacity);
  connections                                = allocator.allocate<Connection>(connections_capacity);
  editor_data.positions                      = allocator.allocate<Vec2>(entities_capacity);
  editor_data.positions_before_grab_movement = allocator.allocate<Vec2>(entities_capacity);

  editor_data.is_selected = allocator.allocate<uint8_t>(entities_capacity);

  SDL_memset(editor_data.is_selected, SDL_FALSE, entities_capacity);

  SDL_RWops* rw = SDL_RWFromFile(default_script_file_name, "rb");
  // SDL_RWops* rw = nullptr;
  if (rw)
  {
    const uint32_t size = static_cast<uint32_t>(SDL_RWsize(rw));
    SDL_Log("\"%s\" found (%u bytes) Loading game from external source", default_script_file_name, size);
    load_from_handle(rw);
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

    entity_count = SDL_arraysize(initial_nodes);

    std::transform(initial_nodes, initial_nodes + entity_count, nodes, [](const NodeDescription& d) { return d.type; });

    std::transform(initial_nodes, initial_nodes + entity_count, editor_data.positions,
                   [](const NodeDescription& d) { return d.position; });

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

  editor_data.zoom = 1.0f;
  node_states[0]   = State::Active;

  editor_data.palette_default  = Palette::generate_happyhue_13();
  editor_data.palette_debugger = Palette::generate_happyhue_3();
}

struct FileOps
{
  explicit FileOps(SDL_RWops* handle)
      : handle(handle)
  {
  }

  template <typename T> void serialize(const T& data)
  {
    SDL_RWwrite(handle, &data, sizeof(T), 1);
  }

  template <typename T> void serialize(const T* data, uint32_t count)
  {
    SDL_RWwrite(handle, data, sizeof(T), count);
  }

  template <typename T> void deserialize(T& data)
  {
    SDL_RWread(handle, &data, sizeof(T), 1);
  }

  template <typename T> void deserialize(T* data, uint32_t count)
  {
    SDL_RWread(handle, data, sizeof(T), count);
  }

  SDL_RWops* handle;
};

void Data::load_from_handle(SDL_RWops* handle)
{
  FileOps s(handle);

  s.deserialize(entity_count);
  s.deserialize(nodes, entity_count);
  s.deserialize(editor_data.positions, entity_count);
  s.deserialize(target_positions_count);
  s.deserialize(target_positions, target_positions_count);
  s.deserialize(connections_count);
  s.deserialize(connections, connections_count);

  SDL_memcpy(editor_data.positions_before_grab_movement, editor_data.positions, sizeof(Vec2) * entity_count);
  SDL_memset(editor_data.is_selected, SDL_FALSE, sizeof(uint8_t) * entity_count);

  reset_graph_state();
}

void Data::save_to_handle(SDL_RWops* handle)
{
  FileOps s(handle);

  s.serialize(entity_count);
  s.serialize(nodes, entity_count);
  s.serialize(editor_data.positions, entity_count);
  s.serialize(target_positions_count);
  s.serialize(target_positions, target_positions_count);
  s.serialize(connections_count);
  s.serialize(connections, connections_count);
}

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

void draw_connection_dot(ImDrawList* draw_list, const ImVec2& position, const float size)
{
  draw_list->AddCircleFilled(position, size, ImColor(30, 30, 30, 200));
}

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

void EditorData::recalculate_selection_box()
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
  default:
    return "N/A";
  }
}

static ImColor to_imgui(const Palette::RGB& rgb, int alpha)
{
  return {rgb.r, rgb.g, rgb.b, alpha};
}

void Data::reset_graph_state()
{
  std::fill(node_states, node_states + entity_count, State::Upcoming);
  auto it = std::find(nodes, nodes + entity_count, Node::Start);
  SDL_assert((nodes + entity_count) != it);
  node_states[std::distance(nodes, it)] = State::Active;
}

void Data::imgui_update()
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
    ImVec2         offset                 = to_imvec2(editor_data.calc_blackboard_offset().scale(editor_data.zoom));
    float          grid                   = 32.0f * editor_data.zoom;
    ImColor        grid_line_color        = to_imgui(editor_data.get_palette().background, 120);
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
  if (editor_data.is_selection_box_active())
  {
    editor_data.recalculate_selection_box();
    SDL_memset(editor_data.is_selected, SDL_FALSE, entity_count);

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      const NodeBox&  render_params = select(nodes[i]);
      const ScaledBox box           = ScaledBox(editor_data.positions[i], render_params.size, editor_data.zoom,
                                      editor_data.calc_blackboard_offset());

      const Vec2 ul = Vec2(box.left, box.up);
      const Vec2 br = Vec2(box.right, box.bottom);

      if (is_intersecting(editor_data.selection_box_ul, editor_data.selection_box_br, ul, br))
      {
        editor_data.is_selected[i] = SDL_TRUE;
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // Precaching color values
  //
  //////////////////////////////////////////////////////////////////////////////

  const ImColor color_upcoming = to_imgui(editor_data.palette_debugger.paragraph, 255);
  const ImColor color_active   = to_imgui(editor_data.palette_debugger.tertiary, 255);
  const ImColor color_finished = to_imgui(editor_data.palette_debugger.button, 255);
  const ImColor color_regular  = to_imgui(editor_data.palette_default.secondary, 255);
  const ImColor color_special  = to_imgui(editor_data.palette_default.tertiary, 255);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Story node rendering
  //
  //////////////////////////////////////////////////////////////////////////////
  for (uint32_t i = 0; i < entity_count; ++i)
  {
    const NodeBox&  render_params = select(nodes[i]);
    const ScaledBox box =
        ScaledBox(editor_data.positions[i], render_params.size, editor_data.zoom, editor_data.calc_blackboard_offset());
    const ImVec2 ul = ImVec2(box.left, box.up);
    const ImVec2 br = ImVec2(box.right, box.bottom);

    // debug node parameters
    if (editor_data.is_selected[i])
    {
      const Vec2& position = editor_data.positions[i];
      ImGui::Text("selected: %s entity index: %u position: [%.2f %.2f] state: %s", render_params.name, i, position.x,
                  position.y, state_to_string(node_states[i]));
    }

    // @TODO draw only if on screen

    if (editor_data.is_showing_state)
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

    if (editor_data.is_selected[i])
    {
      draw_list->AddRect(ul, br, rainbow_color(), 5.0f, ImDrawCornerFlags_All, 4.0f);
    }
    else
    {
      draw_list->AddRect(ul, br, ImColor(0, 0, 0, 210), 5.0f, ImDrawCornerFlags_All, 2.0f);
    }

    if (0.3f < editor_data.zoom)
    {
      const ImVec2 ul_text = ImVec2(ul.x + 5.0f, ul.y + 5.0f);
      draw_list->AddText(ul_text, to_imgui(editor_data.get_palette().button_text, 255), render_params.name);
    }

    for (uint32_t i = 0; i < render_params.inputs_count; ++i)
    {
      draw_list->AddCircleFilled(box.calculate_input_dot_position(render_params, i), dot_size * editor_data.zoom,
                                 to_imgui(editor_data.get_palette().paragraph, 200));
    }

    for (uint32_t i = 0; i < render_params.outputs_count; ++i)
    {
      draw_list->AddCircleFilled(box.calculate_output_dot_position(render_params, i), dot_size * editor_data.zoom,
                                 to_imgui(editor_data.get_palette().paragraph, 200));
    }
  }

  for (uint32_t i = 0; i < connections_count; ++i)
  {
    const Connection& connection = connections[i];

    const NodeBox& src_render_params = select(nodes[connection.src_node_idx]);
    const NodeBox& dst_render_params = select(nodes[connection.dst_node_idx]);

    const ScaledBox src_box(editor_data.positions[connection.src_node_idx], src_render_params.size, editor_data.zoom,
                            editor_data.calc_blackboard_offset());
    const ScaledBox dst_box(editor_data.positions[connection.dst_node_idx], dst_render_params.size, editor_data.zoom,
                            editor_data.calc_blackboard_offset());

    draw_connection_bezier(draw_list,
                           src_box.calculate_output_dot_position(src_render_params, connection.src_output_idx),
                           dst_box.calculate_input_dot_position(dst_render_params, connection.dst_input_idx),
                           to_imgui(editor_data.get_palette().paragraph, 200));
  }

  if (editor_data.connection_building_active)
  {
    const Vec2      mouse         = get_mouse_state();
    const NodeBox&  render_params = select(nodes[editor_data.element_clicked]);
    const ScaledBox src_box       = ScaledBox(editor_data.positions[editor_data.connection_building_idx_clicked_first],
                                        render_params.size, editor_data.zoom, editor_data.calc_blackboard_offset());

    const ImVec2 src_point =
        editor_data.connection_building_input_clicked
            ? src_box.calculate_input_dot_position(render_params, editor_data.connection_building_dot_idx)
            : src_box.calculate_output_dot_position(render_params, editor_data.connection_building_dot_idx);

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
      };

      for (const Combination& combination : combinations)
      {
        if (ImGui::MenuItem(combination.name))
        {
          const uint32_t node_idx = entity_count++;
          nodes[node_idx]         = combination.type;

          const Vec2 position =
              editor_data.rmb.last_position.scale(1.0f / editor_data.zoom) - editor_data.calc_blackboard_offset();

          editor_data.positions[node_idx]                      = position;
          editor_data.positions_before_grab_movement[node_idx] = position;
        }
      }
      ImGui::EndMenu();
    }

    {
      auto to_state_string = [](bool state) { return state ? "Disable debugger" : "Enable debugger"; };
      if (ImGui::MenuItem(to_state_string(editor_data.is_showing_state)))
      {
        editor_data.is_showing_state = !editor_data.is_showing_state;
      }
    }

    if (ImGui::MenuItem("Reset view"))
    {
      editor_data.zoom                     = 1.0f;
      editor_data.blackboard_origin_offset = Vec2(0.0f, 0.0f);
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
        load_from_handle(handle);
        SDL_RWclose(handle);
        SDL_Log("Loaded file %s", default_script_file_name);
      }

      if (ImGui::MenuItem("Save default"))
      {
        SDL_RWops* handle = SDL_RWFromFile(default_script_file_name, "wb");
        save_to_handle(handle);
        SDL_RWclose(handle);
        SDL_Log("Saved file %s", default_script_file_name);
      }

      ImGui::EndMenu();
    }

    ImGui::EndPopup();
  }

  if (editor_data.is_selection_box_active())
  {
    draw_selection_box(draw_list, editor_data.selection_box_ul, editor_data.selection_box_br);
  }
}

void EditorData::handle_mouse_wheel(float val)
{
  zoom = clamp(zoom + val, 0.1f, 10.0f);
}

void EditorData::handle_mouse_motion(const Data& data, const Vec2& motion)
{
  if (lmb)
  {
    lmb.update(motion);

    for (uint32_t i = 0; i < data.entity_count; ++i)
    {
      if (is_selected[i] and (not is_selection_box_active()))
      {
        positions[i] = positions_before_grab_movement[i] +
                       lmb.offset.scale(1.0f / zoom); // - blackboard_origin_offset.scale(1.0f / zoom);
      }
    }
  }
  else if (mmb)
  {
    mmb.update(motion.scale(1.0f / zoom));
  }
}

bool operator==(const Connection& lhs, const Connection& rhs)
{
  return (lhs.src_node_idx == rhs.src_node_idx) and (lhs.src_output_idx == rhs.src_output_idx) and
         (lhs.dst_input_idx == rhs.dst_input_idx) and (lhs.dst_node_idx == rhs.dst_node_idx);
}

void Data::push_connection(const Connection& new_connection)
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

void EditorData::select_element_at_position(const Vec2& position, Data& data)
{
  element_clicked = false;
  for (uint32_t node_idx = 0; node_idx < data.entity_count; ++node_idx)
  {
    const uint32_t  reverse_node_idx = data.entity_count - node_idx - 1;
    const NodeBox&  render_params    = select(data.nodes[reverse_node_idx]);
    const ScaledBox box = ScaledBox(positions[reverse_node_idx], render_params.size, zoom, calc_blackboard_offset());
    const Vec2      ul  = Vec2(box.left, box.up);
    const Vec2      br  = Vec2(box.right, box.bottom);

    if (is_point_enclosed(ul, br, lmb.origin))
    {
      element_clicked = true;

      if (SDL_FALSE == is_selected[reverse_node_idx])
      {
        SDL_memset(is_selected, SDL_FALSE, data.entity_count);
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

              data.push_connection(new_connection);
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

              data.push_connection(new_connection);
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
    SDL_memset(is_selected, SDL_FALSE, data.entity_count);
    connection_building_active = false;
  }

  selection_box_active = true;
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

bool EditorData::is_any_selected(uint32_t count) const
{
  uint8_t* end = &is_selected[count];
  return end != std::find(is_selected, end, SDL_TRUE);
}

void Data::dump_connections() const
{
  for (uint32_t i = 0; i < connections_count; ++i)
  {
    const Connection& c = connections[i];
    SDL_Log("src_node_idx: %u, src_output_idx: %u, dst_input_idx: %u, dst_node_idx: %u", //
            c.src_node_idx, c.src_output_idx, c.dst_input_idx, c.dst_node_idx);
  }
}

void EditorData::remove_selected_nodes(Data& data)
{
  {
    uint32_t removed_entities = 0;
    for (uint8_t* it                               = std::find(is_selected, &is_selected[data.entity_count], SDL_TRUE);
         it != &is_selected[data.entity_count]; it = std::find(it + 1, &is_selected[data.entity_count], SDL_TRUE))
    {
      const uint32_t entity_idx = std::distance(is_selected, it) - removed_entities;

      Connection* new_connections_end = std::remove_if(
          data.connections, &data.connections[data.connections_count], [entity_idx](const Connection& c) {
            return (entity_idx == c.src_node_idx) or (entity_idx == c.dst_node_idx);
          });

      std::for_each(data.connections, new_connections_end, [entity_idx](Connection& c) {
        if (c.src_node_idx > entity_idx)
        {
          c.src_node_idx -= 1;
        }
        if (c.dst_node_idx > entity_idx)
        {
          c.dst_node_idx -= 1;
        }
      });

      data.connections_count = std::distance(data.connections, new_connections_end);
      removed_entities += 1;
    }
  }

  for (uint8_t* it                               = std::find(is_selected, &is_selected[data.entity_count], SDL_TRUE);
       it != &is_selected[data.entity_count]; it = std::find(it, &is_selected[data.entity_count], SDL_TRUE))
  {
    const uint32_t entity_idx = std::distance(is_selected, it);

    auto remove_element = [entity_idx](auto* array, uint32_t end_offset) {
      std::rotate(array + entity_idx, array + entity_idx + 1, &array[end_offset]);
    };

    remove_element(positions, data.entity_count);
    remove_element(positions_before_grab_movement, data.entity_count);
    remove_element(is_selected, data.entity_count);
    remove_element(data.nodes, data.entity_count);
    remove_element(data.node_states, data.entity_count);

    data.entity_count -= 1;
  }
}

void Data::editor_update(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_MOUSEWHEEL:
  {
    if ((not editor_data.mmb.state) and (0.0f != event.wheel.y))
    {
      editor_data.handle_mouse_wheel((0 > event.wheel.y) ? -0.05f : 0.05f);
    }
  }
  break;
  case SDL_MOUSEMOTION:
  {
    editor_data.handle_mouse_motion(*this, to_vec2(event.motion));
  }
  break;
  case SDL_MOUSEBUTTONDOWN:
  {
    switch (event.button.button)
    {
    case SDL_BUTTON_LEFT:
      editor_data.lmb.activate(get_mouse_state());
      editor_data.select_element_at_position(editor_data.lmb.origin, *this);
      break;

    case SDL_BUTTON_RIGHT:
      editor_data.rmb.activate(get_mouse_state());
      break;

    case SDL_BUTTON_MIDDLE:
      editor_data.mmb.activate(get_mouse_state().scale(1.0f / editor_data.zoom));
      break;

    default:
      break;
    }
  }
  break;
  case SDL_MOUSEBUTTONUP:
  {
    switch (event.button.button)
    {
    case SDL_BUTTON_LEFT:
      editor_data.lmb.deactivate();
      if (editor_data.selection_box_active)
      {
        for (uint32_t i = 0; i < entity_count; ++i)
        {
          if (editor_data.is_selected[i])
          {
            editor_data.positions_before_grab_movement[i] = editor_data.positions[i];
          }
        }
      }
      editor_data.selection_box_active = false;
      break;

    case SDL_BUTTON_RIGHT:
      editor_data.rmb.deactivate();
      break;

    case SDL_BUTTON_MIDDLE:
      editor_data.blackboard_origin_offset += editor_data.mmb.offset;
      editor_data.mmb.deactivate();
      break;

    default:
      break;
    }
  }
  break;
  case SDL_KEYDOWN:
    if (SDL_SCANCODE_LSHIFT == event.key.keysym.scancode)
    {
      editor_data.is_shift_pressed = true;
    }
    else if (SDL_SCANCODE_X == event.key.keysym.scancode)
    {
      if ((not editor_data.is_selection_box_active()) and editor_data.is_any_selected(entity_count))
      {
        editor_data.remove_selected_nodes(*this);
      }
    }
    break;
  case SDL_KEYUP:
    if (SDL_SCANCODE_LSHIFT == event.key.keysym.scancode)
    {
      editor_data.is_shift_pressed = false;
    }
    break;
  }
}

} // namespace story
