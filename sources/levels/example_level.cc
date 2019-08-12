#include "example_level.hh"
#include "ecs/manager.hh"
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

namespace {

Vec3 test_movement_update(float t, const ExampleLevel& l)
{
  Vec3 p;
  p.x = SDL_sinf(t);
  p.z = 3.0f + SDL_cosf(t);
  p.y = l.get_height(p.x, p.z) - 1.0f;
  return p;
};

void setup_dynamic_lights(component::Manager& ecs)
{
  {
    auto color_update = [](float t) { return Vec4(20.0f + (5.0f * SDL_sinf(t + 0.4f)), 0.0f, 0.0f, 1.0f); };

    const Entity e = ecs.spawn_entity();
    ecs.positions.insert(e);
    ecs.forced_level_movements.insert(e, test_movement_update);
    ecs.colors.insert(e);
    ecs.color_changes.insert(e, color_update);
  }

  {
    auto movement_update = [](float t, const ExampleLevel& l) {
      Vec3 p;
      p.x = 12.8f * SDL_cosf(t);
      p.z = -10.0f + (8.8f * SDL_sinf(t));
      p.y = l.get_height(p.x, p.z) - 1.0f;
      return p;
    };

    const Entity e = ecs.spawn_entity();
    ecs.positions.insert(e, {1.0f, 1.0f, 1.0f});
    ecs.forced_level_movements.insert(e, movement_update);
    ecs.colors.insert(e, {0.0f, 20.0f, 0.0f, 1.0f});
  }

  {
    auto movement_update = [](float t, const ExampleLevel& l) {
      Vec3 p;
      p.x = 20.8f * SDL_sinf(t / 2.0f);
      p.z = 3.0f + (0.8f * SDL_cosf(t / 2.0f));
      p.y = l.get_height(p.x, p.z) - 1.0f;
      return p;
    };

    const Entity e = ecs.spawn_entity();
    ecs.positions.insert(e, {2.0f, 2.0f, 2.0f});
    ecs.forced_level_movements.insert(e, movement_update);
    ecs.colors.insert(e, {0.0f, 0.0f, 20.0f, 1.0f});
  }

  {
    auto movement_update = [](float t, const ExampleLevel& l) {
      Vec3 p;
      p.x = SDL_sinf(t / 1.2f);
      p.z = 2.5f * SDL_cosf(t / 1.2f);
      p.y = l.get_height(p.x, p.z) - 1.0f;
      return p;
    };

    const Entity e = ecs.spawn_entity();
    ecs.positions.insert(e);
    ecs.forced_level_movements.insert(e, movement_update);
    ecs.colors.insert(e, {8.0f, 8.0f, 8.0f, 1.0f});
  }
}

} // namespace

void ExampleLevel::initialize(component::Manager& ecs) { setup_dynamic_lights(ecs); }
