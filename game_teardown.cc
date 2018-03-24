#include "engine.hh"
#include "game.hh"

void Game::teardown()
{
  for (SDL_Cursor* cursor : mousecursors)
    SDL_FreeCursor(cursor);
  SDL_free(helmet.memory);
  renderableHelmet.teardown(engine);
}
