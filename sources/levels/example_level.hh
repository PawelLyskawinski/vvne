#pragma once

namespace component {
class Manager;
} // namespace component

class ExampleLevel
{
public:
  [[nodiscard]] float get_height(float x, float y) const;
  static void         initialize(component::Manager& ecs);
};
