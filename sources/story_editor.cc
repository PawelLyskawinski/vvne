#include "story_editor.hh"
#include <imgui.h>

namespace story {

namespace {

struct Color
{
  int     r, g, b, a;
  ImColor to_imgui() const { return ImColor(r, g, b, a); }
};

struct NodeBox
{
  const char* name;
  Vec2        size;
  Color       bg;
  Color       font;
};

constexpr NodeBox DummyBox = {
    .name = "Dummy",
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

constexpr const NodeBox& select(Node::Type type)
{
  switch (type)
  {
  default:
  case Node::Type::Dummy:
    return DummyBox;
  case Node::Type::All:
    return AllBox;
  case Node::Type::Any:
    return AnyBox;
  }
}

constexpr float offset_from_top = 25.0f;

} // namespace

void Data::init()
{
  // DEVELOPMENT DEBUGS - REMOVE WHEN NOT NEEDED

  nodes_count = 2;

  nodes[0].type            = Node::Type::Dummy;
  editor_data.positions[0] = Vec2(100.0f, 40.0f);

  nodes[1].type            = Node::Type::All;
  editor_data.positions[1] = Vec2(300.0f, 40.0f);

  editor_data.zoom = 1.0f;
}

void Data::editor_render()
{
  const float   grid            = 32.0f * editor_data.zoom;
  const ImColor grid_line_color = ImColor(0.5f, 0.5f, 0.5f, 0.2f);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  {
    ImVec2 size     = ImGui::GetWindowSize();
    ImVec2 position = ImGui::GetWindowPos();

    position.y += offset_from_top;

    for (float x = 0.0f; x < size.x; x += grid)
    {
      ImVec2 begin(x + position.x, position.y);
      ImVec2 end(x + position.x, size.y + position.y);
      draw_list->AddLine(begin, end, grid_line_color);
    }

    for (float y = 0.0f; y < size.x; y += grid)
    {
      ImVec2 begin(position.x, y + position.y);
      ImVec2 end(size.x + position.x, y + position.y);
      draw_list->AddLine(begin, end, grid_line_color);
    }
  }

  for (uint32_t i = 0; i < nodes_count; ++i)
  {
    const Node&    node          = nodes[i];
    const Vec2&    position      = editor_data.positions[i];
    const NodeBox& render_params = select(node.type);
    const Vec2&    size          = render_params.size;

    const ImVec2 ul      = ImVec2(position.x * editor_data.zoom, offset_from_top + position.y * editor_data.zoom);
    const ImVec2 br      = ImVec2(ul.x + size.x * editor_data.zoom, ul.y + size.y * editor_data.zoom);
    const ImVec2 ul_text = ImVec2(ul.x + 5.0f, ul.y + 5.0f);

    draw_list->AddRectFilled(ul, br, render_params.bg.to_imgui(), 5.0f);
    draw_list->AddRect(ul, br, ImColor(0, 0, 0, 210), 5.0f, ImDrawCornerFlags_All, 2.0f);
    if (editor_data.zoom > 0.3f)
    {
      draw_list->AddText(ul_text, render_params.font.to_imgui(), render_params.name);
    }
  }
}

namespace {

bool is_point_enclosed(const Vec2& ul, const Vec2& br, const Vec2& pt)
{
  return (ul.x <= pt.x) && (br.x >= pt.x) && (ul.y <= pt.y) && (br.y >= pt.y);
}

} // namespace

void Data::editor_update(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_MOUSEWHEEL: {
    if (0.0f != event.wheel.y)
    {
      editor_data.zoom += (0 > event.wheel.y) ? -0.05f : 0.05f;
      editor_data.zoom = clamp(editor_data.zoom, 0.1f, 10.0f);
    }
  }
  break;
  case SDL_MOUSEMOTION: {
    if (editor_data.lmb_clicked)
    {
      const Vec2 event_position      = Vec2(static_cast<float>(event.motion.x), static_cast<float>(event.motion.y));
      editor_data.lmb_clicked_offset = event_position - editor_data.lmb_clicked_position;

      if (editor_data.element_clicked)
      {
        const Vec2& src = editor_data.element_clicked_original_position;
        Vec2&       dst = editor_data.positions[editor_data.element_clicked_idx];

        dst = src + editor_data.lmb_clicked_offset;
      }
    }
  }
  break;
  case SDL_MOUSEBUTTONDOWN: {
    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);

    editor_data.lmb_clicked          = true;
    editor_data.lmb_clicked_position = Vec2(static_cast<float>(x), static_cast<float>(y));

    for (uint32_t i = 0; i < nodes_count; ++i)
    {
      const NodeBox& render_params = select(nodes[i].type);
      const Vec2     ul            = editor_data.positions[i] + Vec2(0.0f, offset_from_top);
      const Vec2     br            = ul + render_params.size.scale(editor_data.zoom);
      if (is_point_enclosed(ul, br, editor_data.lmb_clicked_position))
      {
        editor_data.element_clicked                   = true;
        editor_data.element_clicked_idx               = i;
        editor_data.element_clicked_original_position = ul - Vec2(0.0f, offset_from_top);

        break;
      }
    }
  }
  break;
  case SDL_MOUSEBUTTONUP: {
    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);

    editor_data.lmb_clicked     = false;
    editor_data.element_clicked = false;
  }
  break;
  }
}

} // namespace story
