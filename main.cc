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

  engine->startup();
  game->startup(*engine);

  uint64_t performance_frequency = SDL_GetPerformanceFrequency();
  uint64_t start_of_game_ticks   = SDL_GetPerformanceCounter();

  SDL_ShowWindow(engine->generic_handles.window);
  while (!SDL_QuitRequested())
  {
    uint64_t    start_of_frame_ticks       = SDL_GetPerformanceCounter();
    uint64_t    ticks_from_game_start      = start_of_frame_ticks - start_of_game_ticks;
    float       current_time_sec           = (float)ticks_from_game_start / (float)performance_frequency;
    const int   desired_frames_per_sec     = 60;
    const float desired_frame_duration_sec = (1000.0f / (float)desired_frames_per_sec);

    game->current_time_sec = current_time_sec;
    game->update(*engine, desired_frame_duration_sec);
    game->render(*engine);

    uint64_t frame_time_counter = SDL_GetPerformanceCounter() - start_of_frame_ticks;
    float    elapsed_ms         = 1000.0f * ((float)frame_time_counter / (float)performance_frequency);

    if (elapsed_ms < desired_frame_duration_sec)
      SDL_Delay((uint32_t)SDL_fabs(desired_frame_duration_sec - elapsed_ms));
  }
  SDL_HideWindow(engine->generic_handles.window);

  game->teardown(*engine);
  engine->teardown();

  SDL_free(game);
  SDL_free(engine);
  SDL_Quit();
  return 0;
}
