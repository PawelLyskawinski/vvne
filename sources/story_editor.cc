#include "story_editor.hh"
#include "imgui.h"
#include <SDL2/SDL_log.h>
#include <algorithm>

namespace story {

namespace {

constexpr uint32_t entities_capacity        = 256;
constexpr uint32_t connections_capacity     = 10'240;
constexpr uint32_t components_capacity      = 64;
constexpr float    offset_from_top          = 25.0f;
const char*        default_script_file_name = "default_story_script.bin";

struct Color
{
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 0;

  [[nodiscard]] ImColor to_imgui() const
  {
    return ImColor(r, g, b, a);
  }
};

struct NodeBox
{
  const char* name = "";
  Vec2        size;
  Color       bg;
  Color       font;
};

constexpr NodeBox StartBox = {
    .name = "Dummy",
    .size = Vec2(120.0f, 80.0f),
    .bg   = {80, 106, 137, 220},
    .font = {255, 255, 255, 255},
};

constexpr NodeBox GoToBox = {
    .name = "GoTo",
    .size = Vec2(120.0f, 80.0f),
    .bg   = {80, 106, 137, 220},
    .font = {255, 255, 255, 255},
};

constexpr NodeBox AllBox = {
    .name = "All",
    .size = Vec2(120.0f, 80.0f),
    .bg   = {255, 10, 10, 220},
    .font = {255, 255, 255, 255},
};

constexpr NodeBox AnyBox = {
    .name = "Any",
    .size = Vec2(120.0f, 80.0f),
    .bg   = {80, 106, 137, 220},
    .font = {255, 255, 255, 255},
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

void draw_background_grid(ImDrawList* draw_list, ImVec2 size, ImVec2 position, float grid)
{
  const ImColor  grid_line_color        = ImColor(0.5f, 0.5f, 0.5f, 0.2f);
  const uint32_t vertical_lines_count   = size.x / grid;
  const uint32_t horizontal_lines_count = size.y / grid;

  position.y += offset_from_top;

  for (uint32_t i = 0; i < vertical_lines_count; ++i)
  {
    const float x = grid * i;
    add_vertical_line(draw_list, x + position.x, position.y, size.y, grid_line_color);
  }

  for (uint32_t i = 0; i < horizontal_lines_count; ++i)
  {
    const float y = grid * i;
    add_horizontal_line(draw_list, position.x, y + position.y, size.x, grid_line_color);
  }
}

} // namespace

void Data::init(HierarchicalAllocator& allocator)
{
  nodes                 = allocator.allocate<Node>(entities_capacity);
  node_states           = allocator.allocate<State>(entities_capacity);
  target_positions      = allocator.allocate<TargetPosition>(components_capacity);
  connections           = allocator.allocate<Connection>(connections_capacity);
  editor_data.positions = allocator.allocate<Vec2>(entities_capacity);

  SDL_RWops* rw = SDL_RWFromFile(default_script_file_name, "rb");
  if (rw)
  {
    const uint32_t size = static_cast<uint32_t>(SDL_RWsize(rw));
    SDL_Log("\"%s\" found (%u bytes)! Loading game from external source", default_script_file_name, size);
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
  }

  editor_data.zoom = 1.0f;
  node_states[0]   = State::Active;
}

void Data::load_from_handle(SDL_RWops* handle)
{
  SDL_RWread(handle, &entity_count, sizeof(entity_count), 1);
  SDL_RWread(handle, nodes, sizeof(Node), entity_count);
  SDL_RWread(handle, node_states, sizeof(State), entity_count);
  SDL_RWread(handle, editor_data.positions, sizeof(Vec2), entity_count);
  SDL_RWread(handle, &target_positions_count, sizeof(target_positions_count), 1);
  SDL_RWread(handle, target_positions, sizeof(TargetPosition), target_positions_count);
  SDL_RWread(handle, &connections_count, sizeof(connections_count), 1);
  SDL_RWread(handle, connections, sizeof(Connection), connections_count);
}

void Data::imgui_update()
{
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_background_grid(draw_list, ImGui::GetWindowSize(), ImGui::GetWindowPos(), 32.0f * editor_data.zoom);

  for (uint32_t i = 0; i < entity_count; ++i)
  {
    const Node&    node          = nodes[i];
    const Vec2&    position      = editor_data.positions[i];
    const NodeBox& render_params = select(node);
    const float    up            = offset_from_top + (position.y * editor_data.zoom);
    const float    bottom        = up + (render_params.size.y * editor_data.zoom);
    const float    left          = position.x * editor_data.zoom;
    const float    right         = left + (render_params.size.x * editor_data.zoom);
    const ImVec2   ul            = ImVec2(left, up);
    const ImVec2   br            = ImVec2(right, bottom);

    // @TODO draw only if on screen

    draw_list->AddRectFilled(ul, br, render_params.bg.to_imgui(), 5.0f);
    draw_list->AddRect(ul, br, ImColor(0, 0, 0, 210), 5.0f, ImDrawCornerFlags_All, 2.0f);

    if (0.3f < editor_data.zoom)
    {
      const ImVec2 ul_text = ImVec2(ul.x + 5.0f, ul.y + 5.0f);
      draw_list->AddText(ul_text, render_params.font.to_imgui(), render_params.name);
    }
  }

  if (ImGui::Button("Load"))
  {
    SDL_RWops* handle = SDL_RWFromFile(default_script_file_name, "rb");
    load_from_handle(handle);
    SDL_RWclose(handle);
    SDL_Log("Loaded file %s", default_script_file_name);
  }

  if (ImGui::Button("Save"))
  {
    SDL_RWops* handle = SDL_RWFromFile(default_script_file_name, "wb");

    SDL_RWwrite(handle, &entity_count, sizeof(entity_count), 1);
    SDL_RWwrite(handle, nodes, sizeof(Node), entity_count);
    SDL_RWwrite(handle, node_states, sizeof(State), entity_count);
    SDL_RWwrite(handle, editor_data.positions, sizeof(Vec2), entity_count);
    SDL_RWwrite(handle, &target_positions_count, sizeof(target_positions_count), 1);
    SDL_RWwrite(handle, target_positions, sizeof(TargetPosition), target_positions_count);
    SDL_RWwrite(handle, &connections_count, sizeof(connections_count), 1);
    SDL_RWwrite(handle, connections, sizeof(Connection), connections_count);

    SDL_RWclose(handle);
    SDL_Log("Saved file %s", default_script_file_name);
  }
}

namespace {

[[nodiscard]] bool is_point_enclosed(const Vec2& ul, const Vec2& br, const Vec2& pt)
{
  return (ul.x <= pt.x) && (br.x >= pt.x) && (ul.y <= pt.y) && (br.y >= pt.y);
}

[[nodiscard]] Vec2 to_vec2(const SDL_MouseMotionEvent& event)
{
  return Vec2(static_cast<float>(event.x), static_cast<float>(event.y));
}

[[nodiscard]] Vec2 get_mouse_state()
{
  int x = 0;
  int y = 0;
  SDL_GetMouseState(&x, &y);
  return Vec2(static_cast<float>(x), static_cast<float>(y));
}

} // namespace

void EditorData::handle_mouse_wheel(float val)
{
  zoom = clamp(zoom + val, 0.1f, 10.0f);
}

void EditorData::handle_mouse_motion(const Vec2& motion)
{
  if (lmb_clicked)
  {
    lmb_clicked_offset = motion - lmb_clicked_position;

    if (element_clicked)
    {
      positions[element_clicked_idx] = element_clicked_original_position + lmb_clicked_offset;
    }
  }
}

void EditorData::handle_mouse_lmb_down(const Vec2& position, uint32_t nodes_count, const Node* nodes)
{
  lmb_clicked          = true;
  lmb_clicked_position = position;

  for (uint32_t i = 0; i < nodes_count; ++i)
  {
    const NodeBox& render_params = select(nodes[i]);
    const Vec2     ul            = positions[i] + Vec2(0.0f, offset_from_top);
    const Vec2     br            = ul + render_params.size.scale(zoom);

    if (is_point_enclosed(ul, br, lmb_clicked_position))
    {
      element_clicked                   = true;
      element_clicked_idx               = i;
      element_clicked_original_position = ul - Vec2(0.0f, offset_from_top);
      break;
    }
  }
}

void EditorData::handle_mouse_lmb_up()
{
  lmb_clicked     = false;
  element_clicked = false;
}

void Data::editor_update(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_MOUSEWHEEL:
  {
    if (0.0f != event.wheel.y)
    {
      editor_data.handle_mouse_wheel((0 > event.wheel.y) ? -0.05f : 0.05f);
    }
  }
  break;
  case SDL_MOUSEMOTION:
  {
    editor_data.handle_mouse_motion(to_vec2(event.motion));
  }
  break;
  case SDL_MOUSEBUTTONDOWN:
  {
    editor_data.handle_mouse_lmb_down(get_mouse_state(), entity_count, nodes);
  }
  break;
  case SDL_MOUSEBUTTONUP:
  {
    editor_data.handle_mouse_lmb_up();
  }
  break;
  }
}

} // namespace story
