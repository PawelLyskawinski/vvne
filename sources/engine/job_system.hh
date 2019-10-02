#pragma once

#include "engine_constants.hh"
#include <SDL2/SDL_atomic.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
#include <vulkan/vulkan.h>

struct Stack;

struct ThreadJobData
{
  int    thread_id;
  Stack& allocator;
};

struct Job
{
  inline void operator()(ThreadJobData tjd) { function(tjd, user_data); }

  void (*function)(ThreadJobData, void*);
  void* user_data;
};

struct WorkerCommands
{
  VkCommandBuffer commands[64];
  int             submitted_count;
};

struct WorkerThread
{
  SDL_Thread*    thread_handle;
  VkCommandPool  pool;
  WorkerCommands commands[SWAPCHAIN_IMAGES_COUNT];
};

struct JobSystem
{
  bool thread_end_requested;

  Job          jobs[128];
  uint32_t     jobs_count;
  SDL_atomic_t jobs_taken;

  SDL_cond*    new_jobs_available_cond;
  SDL_mutex*   new_jobs_available_mutex;
  SDL_sem*     all_threads_idle_signal;
  SDL_atomic_t threads_finished_work;
  WorkerThread workers[WORKER_THREADS_COUNT];
  void*        user_data;

  void            setup(VkDevice device, uint32_t graphics_queue_family_index);
  void            teardown(VkDevice device);
  void            reset_command_buffers(uint32_t image_index);
  VkCommandBuffer acquire(uint32_t worker_id, uint32_t image_index);
  void            worker_loop();

  void start();
  void wait_for_finish();
};
