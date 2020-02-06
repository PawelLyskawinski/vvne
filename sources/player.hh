#pragma once

#include "engine/math.hh"
#include <SDL2/SDL_events.h>

//
// Third person view of robot.
//

struct ExampleLevel;

struct Camera
{
  Vec3  position;
  float angle;
  float updown_angle;
};

struct Player
{
  Vec3     position;
  Vec3     velocity;
  Vec3     acceleration;
  Camera   camera;
  Mat4x4   camera_projection;
  Mat4x4   camera_view;
  uint64_t internal_key_flags;

  Camera freecam_camera;
  bool   freecam_mode;
  Vec3   freecam_position;
  Vec3   freecam_velocity;
  Vec3   freecam_acceleration;

  [[nodiscard]] const Camera& get_camera() const;

  void setup(uint32_t width, uint32_t height);
  void process_event(const SDL_Event& event);
  void update(float current_time_sec, float delta_ms, const ExampleLevel& level);
};