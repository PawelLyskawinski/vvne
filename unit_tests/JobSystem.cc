#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_atomic.h>

struct Job
{
  void* arg;
  void (*routine)(void* arg);
};

void job_a(void*)
{
  SDL_Log("from inside job a! will wait for 5 sec");
  SDL_Delay(5000);
}

void job_b(void*)
{
  SDL_Log("from inside job b! will wait for 1 sec");
  SDL_Delay(1000);
}

void job_empty(void*)
{
}

struct JobSystem
{
  Job          jobs[64];
  SDL_atomic_t jobs_taken;
  int          jobs_max;
  SDL_Thread*  worker_threads[4];
  SDL_atomic_t threads_finished_work;
  SDL_sem*     new_jobs_available;
  SDL_sem*     all_threads_idle;
  bool         thread_end_requested;
};

int worker_function(void* arg)
{
  JobSystem* job_system = reinterpret_cast<JobSystem*>(arg);
  int        threadId   = SDL_AtomicIncRef(&job_system->threads_finished_work);
  SDL_Log("[Thread %d] awaiting jobs", threadId);

  while (not job_system->thread_end_requested)
  {
    SDL_SemWait(job_system->new_jobs_available);
    SDL_Log("[Thread %d] starting job processing", threadId);
    SDL_AtomicDecRef(&job_system->threads_finished_work);

    while (true)
    {
      int job_idx = SDL_AtomicIncRef(&job_system->jobs_taken);

      if (job_idx < job_system->jobs_max)
      {
        Job job = job_system->jobs[job_idx];
        job.routine(job.arg);
      }
      else
      {
        break;
      }
    }

    if (3 == SDL_AtomicIncRef(&job_system->threads_finished_work))
    {
      SDL_SemPost(job_system->all_threads_idle);
    }
  }

  SDL_Log("[Thread %d] end requested", threadId);

  return 0;
}

int main(int argc, char* argv[])
{
  uint64_t performance_frequency = SDL_GetPerformanceFrequency();
  uint64_t start_ticks           = SDL_GetPerformanceCounter();

  JobSystem js{};
  js.new_jobs_available   = SDL_CreateSemaphore(0);
  js.all_threads_idle     = SDL_CreateSemaphore(0);
  js.thread_end_requested = false;

  for (auto& worker_thread : js.worker_threads)
    worker_thread = SDL_CreateThread(worker_function, "worker", &js);

  js.jobs[0].routine  = job_a;
  js.jobs[1].routine  = job_b;
  js.jobs[2].routine  = job_empty;
  js.jobs[3].routine  = job_b;
  js.jobs[4].routine  = job_a;
  js.jobs[5].routine  = job_empty;
  js.jobs[6].routine  = job_b;
  js.jobs[7].routine  = job_b;
  js.jobs[8].routine  = job_b;
  js.jobs[9].routine  = job_empty;
  js.jobs[10].routine = job_b;
  js.jobs_max         = 11;

  for (int i = 0; i < 4; ++i)
    SDL_SemPost(js.new_jobs_available);

  SDL_SemWait(js.all_threads_idle);
  js.thread_end_requested = true;
  js.jobs_max             = 0;
  js.jobs_taken.value     = 0;

  for (int i = 0; i < 4; ++i)
    SDL_SemPost(js.new_jobs_available);

  for (auto& worker_thread : js.worker_threads)
  {
    int retval = 0;
    SDL_WaitThread(worker_thread, &retval);
  }

  uint64_t end_ticks = SDL_GetPerformanceCounter();
  SDL_Log("time passed: %f", static_cast<float>(end_ticks - start_ticks) / static_cast<float>(performance_frequency));

  return 0;
}
