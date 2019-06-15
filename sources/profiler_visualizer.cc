#include "profiler_visualizer.hh"
#include "engine/allocators.hh"
#include "profiler.hh"
#include <SDL2/SDL_timer.h>
#include <imgui.h>

namespace {

float to_ms(uint64_t ticks, int64_t freq) { return 1024.0f * static_cast<float>(ticks) / static_cast<float>(freq); }

class MarkerView
{
public:
  explicit MarkerView(const Profiler& p)
      : markers(p.last_frame_markers)
      , n(p.last_frame_markers_count)
  {
  }

  const Marker* begin() const { return markers; }
  const Marker* end() const { return &markers[n]; }

private:
  const Marker* markers;
  uint32_t      n;
};

class ThreadMarkersView
{
public:
  ThreadMarkersView(const Marker* markers, uint32_t n, uint32_t thread_id)
      : it(markers, &markers[n], thread_id)
  {
  }

  ThreadMarkersView(const Profiler& p, uint32_t filter)
      : ThreadMarkersView(p.last_frame_markers, p.last_frame_markers_count, filter)
  {
  }

  class Iterator
  {
  public:
    Iterator(const Marker* begin, const Marker* end, uint32_t filter)
        : current(begin)
        , end(end)
        , thread_filter(filter)
    {
      while ((current != end) and (thread_filter != current->worker_idx))
        ++current;
    }

    Iterator& operator++()
    {
      ++current;
      while ((current != end) and (thread_filter != current->worker_idx))
        ++current;
      return *this;
    }

    bool          operator!=(const Iterator& rhs) const { return current != rhs.current; }
    const Marker* operator*() const { return current; }
    Iterator      as_last() const { return {end, end, thread_filter}; }

  private:
    const Marker* current;
    const Marker* end;
    uint32_t      thread_filter;
  };

  Iterator begin() const { return it; }
  Iterator end() const { return it.as_last(); }

private:
  Iterator it;
};

class ButtonRenderer
{
private:
  void calculate_max_marker_duration(const Profiler& profiler)
  {
    min = UINT64_MAX;
    max = 0;
    for (auto m : MarkerView(profiler))
    {
      min = SDL_min(min, m.begin);
      max = SDL_max(max, m.end);
    }
    max_size = max - min;
  }

public:
  explicit ButtonRenderer(const Profiler& p)
      : counter(0)
      , name_buffer{}
      , max_width(ImGui::GetWindowWidth() * 0.98f)
      , freq(SDL_GetPerformanceFrequency())
      , min(UINT64_MAX)
      , max(0)
      , max_size(0)
  {
    calculate_max_marker_duration(p);
  }

  void render(const Marker& m, float y_offset, uint32_t depth, bool highlight)
  {
    (void)depth; // @todo implement real profiler view with markers in other marker scope
    const float length = max_width * (static_cast<float>(m.end - m.begin) / static_cast<float>(max_size));
    const float offset = max_width * (static_cast<float>(m.begin - min) / static_cast<float>(max_size));

    SDL_snprintf(name_buffer, 128, "%s##%u", "profiler_visualize", counter++);
    ImGui::SetCursorPos(ImVec2(offset, y_offset));

    ImVec4 color;
    if (highlight)
    {
      const Uint64 ticks = SDL_GetTicks();

      color.x = static_cast<float>(ticks % 100u) / 100.0f;
      color.y = static_cast<float>(ticks % 300u) / 300.0f;
      color.z = static_cast<float>(ticks % 200u) / 200.0f;
      color.w = 1.0f;
    }
    else
    {
      auto        clamp     = [](float min, float max, float val) { return val > max ? max : (val < min ? min : val); };
      const float intensity = clamp(0.0f, 1.0f, 0.85f - (length / max_width));
      color                 = ImVec4(intensity, intensity, 1.0f, 1.0f);
    }

    ImGui::ColorButton(name_buffer, color, ImGuiColorEditFlags_NoTooltip, ImVec2(length, 15));

    if (ImGui::IsItemHovered())
    {
      ImGui::BeginTooltip();
      ImGui::Text("%s", m.name);
      ImGui::Text("ticks:        %llu", m.end - m.begin);
      ImGui::Text("duration_ms:  %.4f", 1000.0f * static_cast<float>(m.end - m.begin) / static_cast<float>(freq));
      ImGui::Text("duration_sec: %.4f", static_cast<float>(m.end - m.begin) / static_cast<float>(freq));
      ImGui::EndTooltip();
    }
  }

private:
  uint32_t counter;
  char     name_buffer[128];
  float    max_width;
  uint64_t freq;
  uint64_t min;
  uint64_t max;
  uint64_t max_size;
};

struct MarkerStack
{
  uint32_t push(const Marker& marker)
  {
    if (0 == size)
    {
      stack[0] = marker;
    }
    else
    {
      // deplete markers on stack which already ended
      while ((0 < size) and stack[size - 1].end < marker.begin)
        --size;
      stack[size] = marker;
    }
    return size++;
  }

  Marker   stack[32] = {};
  uint32_t size      = 0;
};

} // namespace

void profiler_visualize(const Profiler& profiler, const char* context_name, const char* highlight_filter,
                        const float base_y_offset)
{
  ButtonRenderer r(profiler);

  ImGui::NewLine();
  for (uint32_t thread_id = 0; thread_id < SDL_arraysize(profiler.workers); ++thread_id)
  {
    // ImGui::Text("thread %d", thread_id);
    // ImGui::Separator();

    MarkerStack stack;
    for (const auto& it : ThreadMarkersView(profiler, thread_id))
    {
      const bool use_highlight = ('\0' != *highlight_filter) and SDL_strstr(it->name, highlight_filter);
      r.render(*it, base_y_offset + (15.0f * thread_id), stack.push(*it), use_highlight);
      ImGui::SameLine();
    }

    ImGui::NewLine();
  }
}
