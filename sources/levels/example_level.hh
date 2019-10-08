#pragma once

#include "simple_entity.hh"
#include <SDL2/SDL_events.h>

struct Materials;

struct WeaponSelection
{
public:
  void init();
  void select(int new_dst);
  void animate(float step);
  void calculate(float transparencies[3]);

private:
  int   src;
  int   dst;
  bool  switch_animation;
  float switch_animation_time;
};

class ExampleLevel
{
public:
  void setup(FreeListAllocator& allocator, const Materials& materials);
  void teardown();
  void process_event(const SDL_Event& event);
  void update(float time_delta_since_last_frame_ms);

  static Job* copy_update_jobs(Job* dst);
  static Job* copy_render_jobs(Job* dst);

  [[nodiscard]] float get_height(float x, float y) const;

  float           booster_jet_fuel;
  WeaponSelection weapon_selections[2];
  SimpleEntity    helmet_entity;
  SimpleEntity    robot_entity;
  SimpleEntity    box_entities[7];
  SimpleEntity    matrioshka_entity;
  SimpleEntity    monster_entity;
  SimpleEntity    rigged_simple_entity;
  SimpleEntity    axis_arrow_entities[3];
};
