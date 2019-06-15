#include "example_level.hh"
#include <SDL2/SDL_stdinc.h>

//
// Ideally this should be the same formula as in the tesselation evaluation shader..
// but we'll see how this ends up.
//

float ExampleLevel::get_terrain_height(float x, float y) const
{
  return 0.05f * (SDL_cosf(4.0f * x) + SDL_cosf(4.0f * y)) + 0.1f;
}
