#define SDL_MAIN_HANDLED
#include "engine.hh"
#include "game.hh"
#include <SDL2/SDL.h>

int main(int argc, char* argv[])
{
  (void)argc;
  (void)argv;

  SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
  SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");
  SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  Engine* engine = reinterpret_cast<Engine*>(SDL_calloc(1, sizeof(Engine)));
  Game*   game   = reinterpret_cast<Game*>(SDL_calloc(1, sizeof(Game)));

  // ----- DEFAULT CONFIGS -----
  engine->MSAA_SAMPLE_COUNT            = VK_SAMPLE_COUNT_8_BIT;
  constexpr int desired_frames_per_sec = 60;
  // ---------------------------

  {
    bool vulkan_validation = false;
    if (argc > 1)
    {
      for (int i = 1; i < argc; ++i)
      {
        if (0 == SDL_strcmp(argv[i], "--validate"))
        {
          vulkan_validation = true;
          break;
        }
      }
    }

    engine->startup(vulkan_validation);
  }

  game->startup(*engine);

  uint64_t        performance_frequency     = SDL_GetPerformanceFrequency();
  uint64_t        start_of_game_ticks       = SDL_GetPerformanceCounter();
  constexpr float desired_frame_duration_ms = (1000.0f / static_cast<float>(desired_frames_per_sec));
  float           elapsed_ms                = desired_frame_duration_ms;

  SDL_ShowWindow(engine->window);
  while (!SDL_QuitRequested())
  {
    uint64_t start_of_frame_ticks  = SDL_GetPerformanceCounter();
    uint64_t ticks_from_game_start = start_of_frame_ticks - start_of_game_ticks;
    float    current_time_sec = static_cast<float>(ticks_from_game_start) / static_cast<float>(performance_frequency);

    game->current_time_sec = current_time_sec;
    game->update(*engine, SDL_max(elapsed_ms, desired_frame_duration_ms));
    game->render(*engine);

    uint64_t frame_time_counter = SDL_GetPerformanceCounter() - start_of_frame_ticks;
    elapsed_ms = 1000.0f * (static_cast<float>(frame_time_counter) / static_cast<float>(performance_frequency));

    if (elapsed_ms < desired_frame_duration_ms)
    {
      const uint32_t wait_time = static_cast<uint32_t>(SDL_ceilf(desired_frame_duration_ms - elapsed_ms));
      if (0 < wait_time)
        SDL_Delay(wait_time);
    }
  }
  SDL_HideWindow(engine->window);

  game->teardown(*engine);
  engine->teardown();

  SDL_free(game);
  SDL_free(engine);
  SDL_Quit();
  return 0;
}
