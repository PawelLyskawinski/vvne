#include "profiler.hh"

void Profiler::on_frame()
{
  const uint64_t before_clear = SDL_AtomicSet(&last_marker_idx, 0);
  if (not paused)
  {
    last_frame_markers_count = before_clear;
    SDL_memcpy(last_frame_markers, markers, sizeof(Marker) * before_clear);
  }
}

Marker* Profiler::request_marker() { return &markers[SDL_AtomicIncRef(&last_marker_idx)]; }

ScopedPerfEvent::ScopedPerfEvent(Profiler& profiler, const char* name, uint32_t thread_id)
    : ctx(profiler.workers[thread_id])
{
  Marker* marker              = profiler.request_marker();
  ctx.stack[ctx.stack_size++] = marker;

  marker->name       = name;
  marker->begin      = SDL_GetPerformanceCounter();
  marker->worker_idx = thread_id;
}

ScopedPerfEvent::~ScopedPerfEvent()
{
  Marker* last_marker = ctx.stack[--ctx.stack_size];
  last_marker->end    = SDL_GetPerformanceCounter();
}
