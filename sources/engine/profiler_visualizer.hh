#pragma once

struct Profiler;
void profiler_visualize(const Profiler& profiler, const char* context_name, const char* highlight_filter, const float base_y_offset);
