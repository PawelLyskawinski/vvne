#include "job_system.hh"

void JobSystem::setup(VkDevice device, uint32_t graphics_queue_family_index)
{
  all_threads_idle_signal  = SDL_CreateSemaphore(0);
  new_jobs_available_cond  = SDL_CreateCond();
  new_jobs_available_mutex = SDL_CreateMutex();
  thread_end_requested     = false;

  for (WorkerThread& worker : workers)
  {
    {
      VkCommandPoolCreateInfo info = {
          .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = graphics_queue_family_index,
      };

      vkCreateCommandPool(device, &info, nullptr, &worker.pool);
    }

    for (WorkerCommands& cmds : worker.commands)
    {
      VkCommandBufferAllocateInfo info = {
          .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool        = worker.pool,
          .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
          .commandBufferCount = SDL_arraysize(cmds.commands),
      };

      vkAllocateCommandBuffers(device, &info, cmds.commands);
    }
  }

  SDL_AtomicSet(&threads_finished_work, 1);
  for (auto& worker : workers)
  {
    worker.thread_handle = SDL_CreateThread(
        [](void* arg) {
          reinterpret_cast<JobSystem*>(arg)->worker_loop();
          return 0;
        },
        "worker", this);
  }
  SDL_SemWait(all_threads_idle_signal);
  SDL_AtomicSet(&threads_finished_work, 1);
}

void JobSystem::teardown(VkDevice device)
{
  thread_end_requested = true;
  jobs_count           = 0;
  SDL_AtomicSet(&jobs_taken, 0);

  SDL_CondBroadcast(new_jobs_available_cond);

  for (WorkerThread& worker : workers)
  {
    SDL_WaitThread(worker.thread_handle, nullptr);
  }

  SDL_DestroySemaphore(all_threads_idle_signal);
  SDL_DestroyCond(new_jobs_available_cond);
  SDL_DestroyMutex(new_jobs_available_mutex);

  for (WorkerThread& worker : workers)
    vkDestroyCommandPool(device, worker.pool, nullptr);
}

void JobSystem::reset_command_buffers(uint32_t image_index)
{
  for (WorkerThread& worker : workers)
  {
    WorkerCommands& cmds = worker.commands[image_index];
    for (int i = 0; i < cmds.submitted_count; ++i)
      vkResetCommandBuffer(cmds.commands[i], 0);
    cmds.submitted_count = 0;
  }
}

VkCommandBuffer JobSystem::acquire(uint32_t worker_index, uint32_t image_index)
{
  WorkerThread&   worker   = workers[worker_index - 1];
  WorkerCommands& commands = worker.commands[image_index];

  return commands.commands[commands.submitted_count++];
}

void JobSystem::worker_loop()
{
  int threadId = SDL_AtomicIncRef(&threads_finished_work);
  if (SDL_arraysize(workers) == threadId)
    SDL_SemPost(all_threads_idle_signal);

  Stack allocator{};
  allocator.setup(1024);

  SDL_LockMutex(new_jobs_available_mutex);
  while (not thread_end_requested)
  {
    SDL_CondWait(new_jobs_available_cond, new_jobs_available_mutex);
    SDL_UnlockMutex(new_jobs_available_mutex);

    uint32_t job_idx = SDL_AtomicIncRef(&jobs_taken);

    while (job_idx < jobs_count)
    {
      ThreadJobData tjd = {
          .thread_id = threadId,
          .allocator = allocator,
          .user_data = user_data,
      };

      jobs[job_idx](tjd);
      allocator.reset();
      job_idx = SDL_AtomicIncRef(&jobs_taken);
    }

    SDL_LockMutex(new_jobs_available_mutex);
    if (SDL_arraysize(workers) == SDL_AtomicIncRef(&threads_finished_work))
      SDL_SemPost(all_threads_idle_signal);
  }
  SDL_UnlockMutex(new_jobs_available_mutex);
  allocator.teardown();
}

void JobSystem::start()
{
  SDL_LockMutex(new_jobs_available_mutex);
  SDL_CondBroadcast(new_jobs_available_cond);
  SDL_UnlockMutex(new_jobs_available_mutex);
}

void JobSystem::wait_for_finish()
{
  SDL_SemWait(all_threads_idle_signal);
  SDL_AtomicSet(&threads_finished_work, 1);
  SDL_AtomicSet(&jobs_taken, 0);
}
