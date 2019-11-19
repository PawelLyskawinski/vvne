#pragma once

#include "engine/engine_constants.hh"
#include <SDL2/SDL_atomic.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>

struct Marker
{
  const char* name;
  uint64_t    begin;
  uint64_t    end;
  uint32_t    worker_idx;
};

struct WorkerContext
{
  Marker*  stack[64];
  uint32_t stack_size;
};

struct Profiler
{
  WorkerContext workers[WORKER_THREADS_COUNT + 1];

  //
  // configuration
  //

  // skip feature lets you configure lag between captured frames
  // "0" means real time per frame measurements
  // any other value activates the lag
  int skip_frames;
  int skip_counter;

  //
  // current frame
  //
  Marker       markers[500];
  SDL_atomic_t last_marker_idx;

  //
  // historic data
  //
  Marker   last_frame_markers[SDL_arraysize(markers)];
  uint32_t last_frame_markers_count;
  bool     paused;

  void    on_frame();
  Marker* request_marker();
};

struct ScopedPerfEvent
{
  WorkerContext& ctx;

  ScopedPerfEvent(Profiler& profiler, const char* name, uint32_t thread_id);
  ~ScopedPerfEvent();
};
