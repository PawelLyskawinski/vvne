#include "example_level.hh"
#include "materials.hh"
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_stdinc.h>

namespace {

float ease_in_out_quart(float t)
{
  if (t < 0.5)
  {
    t *= t;
    return 8 * t * t;
  }
  else
  {
    t = (t - 1.0f) * t;
    return 1 - 8 * t * t;
  }
}

} // namespace

void WeaponSelection::init()
{
  src                   = 1;
  dst                   = 1;
  switch_animation      = false;
  switch_animation_time = 0.0f;
}

void WeaponSelection::select(int new_dst)
{
  if ((not switch_animation) and (new_dst != src))
  {
    dst                   = new_dst;
    switch_animation      = true;
    switch_animation_time = 0.0f;
  }
}

void WeaponSelection::animate(float step)
{
  if (not switch_animation)
    return;

  switch_animation_time += step;
  if (switch_animation_time > 1.0f)
  {
    switch_animation_time = 1.0f;
    switch_animation      = false;
    src                   = dst;
  }
}

void WeaponSelection::calculate(float transparencies[3])
{
  const float highlighted_value = 1.0f;
  const float dimmed_value      = 0.4f;

  if (not switch_animation)
  {
    for (int i = 0; i < 3; ++i)
      transparencies[i] = (i == dst) ? highlighted_value : dimmed_value;
  }
  else
  {

    for (int i = 0; i < 3; ++i)
    {
      if (i == src)
      {
        transparencies[i] = 1.0f - (0.6f * ease_in_out_quart(switch_animation_time));
      }
      else if (i == dst)
      {
        transparencies[i] = 0.4f + (0.6f * ease_in_out_quart(switch_animation_time));
      }
      else
      {
        transparencies[i] = 0.4f;
      }
    }
  }
}

void ExampleLevel::setup(HierarchicalAllocator& allocator, const Materials& materials)
{
  helmet_entity.init(allocator, materials.helmet);
  robot_entity.init(allocator, materials.robot);
  monster_entity.init(allocator, materials.monster);

  for (SimpleEntity& entity : box_entities)
  {
    entity.init(allocator, materials.box);
  }

  matrioshka_entity.init(allocator, materials.animatedBox);
  rigged_simple_entity.init(allocator, materials.riggedSimple);

  for (SimpleEntity& entity : axis_arrow_entities)
  {
    entity.init(allocator, materials.lil_arrow);
  }

  booster_jet_fuel = 1.0f;

  for (WeaponSelection& sel : weapon_selections)
  {
    sel.init();
  }
}

void ExampleLevel::teardown() {}

void ExampleLevel::process_event(const SDL_Event& event)
{
  switch (event.type)
  {
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    switch (event.key.keysym.scancode)
    {
    case SDL_SCANCODE_1:
      weapon_selections[0].select(0);
      break;
    case SDL_SCANCODE_2:
      weapon_selections[0].select(1);
      break;
    case SDL_SCANCODE_3:
      weapon_selections[0].select(2);
      break;
    case SDL_SCANCODE_4:
      weapon_selections[1].select(0);
      break;
    case SDL_SCANCODE_5:
      weapon_selections[1].select(1);
      break;
    case SDL_SCANCODE_6:
      weapon_selections[1].select(2);
      break;

    default:
      break;
    }
  }
  default:
    break;
  }
}

void ExampleLevel::update(float time_delta_since_last_frame_ms)
{
  for (WeaponSelection& sel : weapon_selections)
  {
    sel.animate(0.008f * time_delta_since_last_frame_ms);
  }
}

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
