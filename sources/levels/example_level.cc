#include "example_level.hh"
#include <SDL2/SDL_stdinc.h>

//
// Ideally this should be the same formula as in the tesselation evaluation shader..
// but we'll see how this ends up.
//

float ExampleLevel::get_height(float x, float y) const
{
  constexpr float adjustment = 0.1f;

  float h = SDL_cosf(adjustment * x) + SDL_cosf(adjustment * y);
  h *= -2.0f;
  h += 12.0f;
  return h;
}
