#pragma once

#include "engine/math.hh"
#include <SDL2/SDL_events.h>

//
// Third person view of robot.
//

struct Player
{
  Vec3     position;
  Vec3     velocity;
  Vec3     acceleration;
  Vec3     camera_position;
  Mat4x4   camera_projection;
  Mat4x4   camera_view;
  float    camera_angle        = 0.0f;
  float    camera_updown_angle = 0.0f;
  uint64_t internal_key_flags  = 0ULL;

  void setup(uint32_t width, uint32_t height);
  void process_event(const SDL_Event& event);
  void update(float current_time_sec, float delta_ms);
};