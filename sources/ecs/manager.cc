#include "manager.hh"

namespace component {

void Manager::init(FreeListAllocator& a)
{
  positions.init(a, 32);
  colors.init(a, 32);
  point_lights.init(a, 32);
  forced_level_movements.init(a, 32);
  color_changes.init(a, 32);
}

} // namespace component
