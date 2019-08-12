#include "ecs/systems.hh"
#include "engine/gltf.hh"
#include "materials.hh"
#include <SDL2/SDL_log.h>
#include <numeric>

namespace {

Entity* intersect(const BaseEntityList** begin, const BaseEntityList** end, Entity* dst)
{
  Entity* dst_end = std::copy((*begin)->begin(), (*begin)->end(), dst);
  return std::accumulate(begin + 1, end, dst_end, [dst](Entity* dst_end, const BaseEntityList* it) {
    return std::set_intersection(dst, dst_end, it->begin(), it->end(), dst);
  });
}

} // namespace

MovementSystem::MovementSystem(component::Manager& ecs, const ExampleLevel& level, float time)
    : current_time(time)
    , level(level)
    , positions(ecs.positions)
    , calculations(ecs.forced_level_movements)
{
}

void MovementSystem::operator()() const
{
  const BaseEntityList* inputs[] = {&positions.entities, &calculations.entities};

  Entity entities[64] = {};
  std::for_each(entities, intersect(inputs, std::end(inputs), entities),
                [this](Entity e) { positions.at(e) = calculations.at(e)(current_time, level); });
}

ColorAnimationSystem::ColorAnimationSystem(component::Manager& ecs, float time)
    : current_time(time)
    , colors(ecs.colors)
    , point_lights(ecs.point_lights)
    , color_changes(ecs.color_changes)
{
}

void ColorAnimationSystem::operator()() const
{
  const BaseEntityList* inputs[] = {&colors.entities, &color_changes.entities};

  Entity entities[64] = {};
  std::for_each(entities, intersect(inputs, std::end(inputs), entities),
                [this](Entity e) { colors.at(e) = color_changes.at(e)(current_time); });
}

PointLightRenderingSystem::PointLightRenderingSystem(const component::Manager& ecs, const Engine& engine,
                                                     const SceneGraph& model, const Materials& materials,
                                                     VkCommandBuffer command_buffer)
    : command_buffer(command_buffer)
    , engine(engine)
    , materials(materials)
    , model(model)
    , positions(ecs.positions)
    , colors(ecs.colors)
    , point_lights(ecs.point_lights)
{
}

void PointLightRenderingSystem::operator()() const
{
  const BaseEntityList* inputs[] = {&positions.entities, &colors.entities, &point_lights.entities};

  Entity  entities[64] = {};
  Entity* entities_end = intersect(inputs, std::end(inputs), entities);

  struct Update
  {
    Vec4     light_positions[64] = {};
    Vec4     light_colors[64]    = {};
    uint32_t count               = 0u;
  } update = {};

  auto entity_to_position = [this](Entity e) { return Vec4(this->positions.at(e)); };
  auto entity_to_color    = [this](Entity e) { return this->colors.at(e); };

  std::transform(entities, entities_end, update.light_positions, entity_to_position);
  std::transform(entities, entities_end, update.light_colors, entity_to_color);
  update.count = std::distance(entities, entities_end);

  Update* data = nullptr;
  vkMapMemory(engine.device, engine.memory_blocks.host_coherent_ubo.memory, materials.pbr_dynamic_lights_ubo_offsets[0],
              sizeof(Update), 0, reinterpret_cast<void**>(&data));
  *data = update;
  vkUnmapMemory(engine.device, engine.memory_blocks.host_coherent_ubo.memory);
}
