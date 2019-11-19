#include "story_editor.hh"

#define IM_VEC2_CLASS_EXTRA
#include <imgui.h>

void story::Data::editor_render()
{
  ImGuiIO&    io        = ImGui::GetIO();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  ImVec2      size     = ImGui::GetWindowSize();
  ImVec2      position = ImGui::GetWindowPos();
  const float grid     = 32.0f;
  const ImColor grid_line_color = ImColor(0.5f, 0.5f, 0.5f, 0.2f);

  position.y += 25.0f;

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

void story::Data::editor_update(const SDL_Event& event)
{
  // ImGuiIO& io = ImGui::GetIO();
  // const ImVec2 mouse = ImGui::GetMousePos();
}
