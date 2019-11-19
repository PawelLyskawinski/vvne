#include "story_editor.hh"
#include <imgui.h>

namespace story {

namespace {

ImColor to_color(const Node::Type& type)
{
  using Type = Node::Type;

  switch (type)
  {
  case Type::Dummy:
    return ImColor(80, 106, 137, 220);
  case Type::All:
    return ImColor(255, 10, 10, 220);
  case Type::Any:
    return ImColor(80, 106, 137, 220);
  }
}

ImColor to_font_color(const Node::Type& type)
{
  using Type = Node::Type;

  switch (type)
  {
  case Type::Dummy:
    return ImColor(255, 255, 255);
  case Type::All:
    return ImColor(0, 0, 0);
  case Type::Any:
    return ImColor(255, 255, 255);
  }
}

Vec2 to_size(const Node::Type& type)
{
  using Type = Node::Type;

  switch (type)
  {
  case Type::Dummy:
    return Vec2(120.0f, 80.0f);
  case Type::All:
    return Vec2(120.0f, 80.0f);
  case Type::Any:
    return Vec2(120.0f, 80.0f);
  }
}

const char* to_name(const Node::Type& type)
{
  using Type = Node::Type;

  switch (type)
  {
  case Type::Dummy:
    return "Dummy";
  case Type::All:
    return "All";
  case Type::Any:
    return "Any";
  }
}

} // namespace

void Data::editor_render()
{
  const float   grid            = 32.0f;
  const ImColor grid_line_color = ImColor(0.5f, 0.5f, 0.5f, 0.2f);
  const float   offset_from_top = 25.0f;

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

  // DEVELOPMENT DEBUGS - REMOVE WHEN NOT NEEDED

  nodes_count = 2;

  nodes[0].type           = Node::Type::Dummy;
  editor_data[0].position = Vec2(100.0f, 40.0f);

  nodes[1].type           = Node::Type::All;
  editor_data[1].position = Vec2(300.0f, 40.0f);

  for (uint32_t i = 0; i < nodes_count; ++i)
  {
    const Node&       node = nodes[i];
    const EditorData& data = editor_data[i];
    const Vec2        size = to_size(node.type);

    const ImVec2 ul = ImVec2(data.position.x, offset_from_top + data.position.y);
    const ImVec2 br = ImVec2(ul.x + size.x, ul.y + size.y);

    draw_list->AddRectFilled(ul, br, to_color(node.type), 5.0f);
    draw_list->AddText(ImGui::GetFont(), 20.0f, ul, to_font_color(node.type), to_name(node.type));
  }
}

void Data::editor_update(const SDL_Event& event)
{
  // ImGuiIO& io = ImGui::GetIO();
  // const ImVec2 mouse = ImGui::GetMousePos();
}

} // namespace story
