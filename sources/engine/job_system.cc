#include "job_system.hh"
#include "vtl/allocators.hh"
#include "vtl/literals.hh"
#include <algorithm>

namespace {

class VulkanInitialization
{
public:
  VulkanInitialization(VkDevice device, uint32_t graphics_queue_family_index)
      : device(device)
      , pool_create_info(create_pool_info(graphics_queue_family_index))
      , cb_allocate_info(create_cb_info())
  {
  }

  [[nodiscard]] VkCommandPool create_pool() const
  {
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &pool_create_info, nullptr, &pool);
    return pool;
  }

  void allocate_command_buffers(VkCommandPool pool, VkCommandBuffer dst[]) const
  {
    VkCommandBufferAllocateInfo info = cb_allocate_info;
    info.commandPool                 = pool;
    vkAllocateCommandBuffers(device, &info, dst);
  }

private:
  static VkCommandPoolCreateInfo create_pool_info(uint32_t graphics_queue_family_index)
  {
    VkCommandPoolCreateInfo i = {};
    i.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    i.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    i.queueFamilyIndex        = graphics_queue_family_index;
    return i;
  }

  static VkCommandBufferAllocateInfo create_cb_info()
  {
    VkCommandBufferAllocateInfo i = {};
    i.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    i.commandPool                 = VK_NULL_HANDLE;
    i.level                       = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    i.commandBufferCount          = sizeof(WorkerCommands::commands) / sizeof(VkCommandBuffer);
    return i;
  }

private:
  VkDevice                    device;
  VkCommandPoolCreateInfo     pool_create_info;
  VkCommandBufferAllocateInfo cb_allocate_info;
};

SDL_Thread* spawn_worker_thread(JobSystem* system)
{
  return SDL_CreateThread(
      [](void* arg) {
        reinterpret_cast<JobSystem*>(arg)->worker_loop();
        return 0;
      },
      "worker", system);
}

} // namespace

void JobSystem::setup(VkDevice device, uint32_t graphics_queue_family_index)
{
  VulkanInitialization vk(device, graphics_queue_family_index);

  for (WorkerThread& worker : workers)
    worker.pool = vk.create_pool();

  for (WorkerThread& worker : workers)
    for (WorkerCommands& commands : worker.commands)
      vk.allocate_command_buffers(worker.pool, commands.commands);

  all_threads_idle_signal  = SDL_CreateSemaphore(0);
  new_jobs_available_cond  = SDL_CreateCond();
  new_jobs_available_mutex = SDL_CreateMutex();
  thread_end_requested     = false;

  SDL_AtomicSet(&threads_finished_work, 1);

  for (WorkerThread& worker : workers)
    worker.thread_handle = spawn_worker_thread(this);

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
    WorkerCommands& worker_commands = worker.commands[image_index];
    std::for_each(worker_commands.commands, &worker_commands.commands[worker_commands.submitted_count],
                  [](VkCommandBuffer& cmd) { vkResetCommandBuffer(cmd, 0); });
    worker_commands.submitted_count = 0;
  }
}

VkCommandBuffer JobSystem::acquire(uint32_t worker_index, uint32_t image_index)
{
  WorkerThread&   worker   = workers[worker_index - 1];
  WorkerCommands& commands = worker.commands[image_index];

  return commands.commands[commands.submitted_count++];
}

struct JobUtilsConcrete : public JobUtils
{
  explicit JobUtilsConcrete(JobSystem& parent, void* user_data)
      : memory(256_KB)
      , user_data(user_data)
      , parent(parent)
      , thread_id(SDL_AtomicIncRef(&parent.threads_finished_work))
  {
  }

  void* get_user_data() override
  {
    return user_data;
  }

  Stack& get_allocator() override
  {
    return memory;
  }

  VkCommandBuffer request_command_buffer(uint32_t image_index) override
  {
    return parent.acquire(thread_id, image_index);
  }

  Stack      memory;
  void*      user_data;
  JobSystem& parent;
  int        thread_id;
};

void JobSystem::worker_loop()
{
  JobUtilsConcrete utils(*this, user_data);

  if (SDL_arraysize(workers) == utils.thread_id)
  {
    SDL_SemPost(all_threads_idle_signal);
  }

  SDL_LockMutex(new_jobs_available_mutex);
  while (not thread_end_requested)
  {
    SDL_CondWait(new_jobs_available_cond, new_jobs_available_mutex);
    SDL_UnlockMutex(new_jobs_available_mutex);

    uint32_t job_idx = SDL_AtomicIncRef(&jobs_taken);

    while (job_idx < jobs_count)
    {
      Job& job = jobs[job_idx];

      {
        ScopedPerfEvent perf_event(*profiler, job.name, utils.thread_id);
        job(utils);
      }

      utils.memory.reset();
      job_idx = SDL_AtomicIncRef(&jobs_taken);
    }

    SDL_LockMutex(new_jobs_available_mutex);
    if (SDL_arraysize(workers) == SDL_AtomicIncRef(&threads_finished_work))
      SDL_SemPost(all_threads_idle_signal);
  }
  SDL_UnlockMutex(new_jobs_available_mutex);
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
