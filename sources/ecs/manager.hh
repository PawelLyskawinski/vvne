#pragma once

#include "ecs/components.hh"
#include "engine/math.hh"

struct ExampleLevel;

namespace component {

using ForcedLevelMovement = Vec3 (*)(float time, const ExampleLevel& level);
using ColorChange         = Vec4 (*)(float time);

struct PointLight
{
  enum class FlickerStyle : uint8_t
  {
    Stable,
  };

  uint8_t      is_active;
  FlickerStyle flicker;
};

class Manager
{
public:
  [[nodiscard]] Entity spawn_entity() { return last_entity++; }

  void init(FreeListAllocator& allocator);

  Components<Vec3>                positions;
  Components<Vec4>                colors;
  Components<PointLight>          point_lights;
  Components<ForcedLevelMovement> forced_level_movements;
  Components<ColorChange>         color_changes;

private:
  Entity last_entity;
};

} // namespace component
