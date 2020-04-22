#pragma once

#include "engine_constants.hh"
#include "profiler.hh"
#include <SDL2/SDL_atomic.h>
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
#include <vulkan/vulkan.h>

struct Stack;

struct JobUtils
{
  virtual ~JobUtils() = default;

  virtual void*           get_user_data()                              = 0;
  virtual Stack&          get_allocator()                              = 0;
  virtual VkCommandBuffer request_command_buffer(uint32_t image_index) = 0;
};

struct WorkerCommands
{
  VkCommandBuffer commands[WORKER_MAX_COMMANDS_PER_FRAME];
  int             submitted_count;
};

struct WorkerThread
{
  SDL_Thread*    thread_handle;
  VkCommandPool  pool;
  WorkerCommands commands[SWAPCHAIN_IMAGES_COUNT];
};

struct Job
{
  using FunctionCall = void (*)(JobUtils&);

  FunctionCall call;
  const char*  name;

  void operator()(JobUtils& utils)
  {
    call(utils);
  }
};

using JobGenerator = Job*(Job*);

struct JobSystem
{
  bool thread_end_requested;

  Job          jobs[MAX_JOBS_PER_FRAME];
  uint32_t     jobs_count;
  SDL_atomic_t jobs_taken;

  SDL_cond*    new_jobs_available_cond;
  SDL_mutex*   new_jobs_available_mutex;
  SDL_sem*     all_threads_idle_signal;
  SDL_atomic_t threads_finished_work;
  WorkerThread workers[WORKER_THREADS_COUNT];
  void*        user_data;
  Profiler*    profiler;

  void            setup(VkDevice device, uint32_t graphics_queue_family_index);
  void            teardown(VkDevice device);
  void            reset_command_buffers(uint32_t image_index);
  VkCommandBuffer acquire(uint32_t worker_id, uint32_t image_index);
  void            worker_loop();
  inline void     fill_jobs(JobGenerator g)
  {
    jobs_count = (g(jobs) - jobs);
  }

  void start();
  void wait_for_finish();
};
