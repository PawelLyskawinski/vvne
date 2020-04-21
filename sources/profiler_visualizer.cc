#include "profiler.hh"
#include "vtl/allocators.hh"
#include <SDL2/SDL_timer.h>
#include <imgui.h>

#include <algorithm>

void profiler_visualize(const Profiler& profiler, const char* context_name, const char* highlight_filter,
                        const float base_y_offset)
{
  uint32_t       counter          = 0;
  char           name_buffer[128] = {};
  const float    max_width        = ImGui::GetWindowWidth() * 0.98f;
  const uint64_t freq             = SDL_GetPerformanceFrequency();
  const Marker*  markers_begin    = profiler.last_frame_markers;
  const Marker*  markers_end      = &profiler.last_frame_markers[profiler.last_frame_markers_count];
  auto           lesser_begin     = [](const Marker& it, const Marker& smallest) { return it.begin < smallest.begin; };
  auto           greater_end      = [](const Marker& largest, const Marker& it) { return largest.end < it.end; };
  const uint64_t min              = std::min_element(markers_begin, markers_end, lesser_begin)->begin;
  const uint64_t max              = std::max_element(markers_begin, markers_end, greater_end)->end;
  const uint64_t max_size         = max - min;

  ImGui::NewLine();

  for (uint32_t thread_id = 0; thread_id < static_cast<uint32_t>(SDL_arraysize(profiler.workers)); ++thread_id)
  {
    for(const Marker* it = markers_begin; it != markers_end; ++it)
    {
      const Marker& marker = *it;
      if (marker.worker_idx != thread_id)
      {
        continue;
      }

      uint64_t    marker_duration_ticks = marker.end - marker.begin;
      const float marker_duration       = static_cast<float>(marker_duration_ticks);
      const float length                = max_width * (marker_duration / static_cast<float>(max_size));
      const float offset = max_width * (static_cast<float>(marker.begin - min) / static_cast<float>(max_size));

      SDL_snprintf(name_buffer, 128, "%s##%u", "profiler_visualize", counter++);
      ImGui::SetCursorPos(ImVec2(offset, base_y_offset + (15.0f * thread_id)));

      ImVec4 color;
      if (('\0' != *highlight_filter) and SDL_strstr(marker.name, highlight_filter))
      {
        auto fuzz = [](Uint64 value, Uint64 div) { return static_cast<float>(value % div) / static_cast<float>(div); };
        const Uint64 ticks = SDL_GetTicks();
        color              = ImVec4(fuzz(ticks, 100u), fuzz(ticks, 300u), fuzz(ticks, 200u), 1.0f);
      }
      else
      {
        const float intensity = std::clamp(0.85f - (length / max_width), 0.0f, 1.0f);
        color                 = ImVec4(intensity, intensity, 1.0f, 1.0f);
      }

      ImGui::ColorButton(name_buffer, color, ImGuiColorEditFlags_NoTooltip, ImVec2(length, 15));

      if (ImGui::IsItemHovered())
      {
        ImGui::BeginTooltip();
        ImGui::Text("%s", marker.name);
        ImGui::Text("ticks:        %u", static_cast<uint32_t>(marker.end - marker.begin));
        ImGui::Text("duration_ms:  %.4f", 1000.0f * marker_duration / static_cast<float>(freq));
        ImGui::Text("duration_sec: %.4f", marker_duration / static_cast<float>(freq));
        ImGui::EndTooltip();
      }

      ImGui::SameLine();
    }
    ImGui::NewLine();
  }
}
