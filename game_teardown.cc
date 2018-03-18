#include "engine.hh"
#include "game.hh"

void game_teardown(Game& game, Engine& engine)
{
  for (SDL_Cursor* cursor : game.mousecursors)
    SDL_FreeCursor(cursor);
  SDL_free(game.helmet.memory);
  game.renderableHelmet.teardown(engine);
}
