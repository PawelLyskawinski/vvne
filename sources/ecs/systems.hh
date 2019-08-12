#pragma once

#include "manager.hh"

struct SceneGraph;
struct Engine;
struct Materials;

class MovementSystem
{
public:
  MovementSystem(component::Manager& ecs, const ExampleLevel& level, float time);
  void operator()() const;

private:
  const float                                       current_time;
  const ExampleLevel&                               level;
  Components<Vec3>&                                 positions;
  const Components<component::ForcedLevelMovement>& calculations;
};

class ColorAnimationSystem
{
public:
  ColorAnimationSystem(component::Manager& ecs, float time);
  void operator()() const;

private:
  const float                               current_time;
  Components<Vec4>&                         colors;
  Components<component::PointLight>         point_lights;
  const Components<component::ColorChange>& color_changes;
};

#if 1
class PointLightRenderingSystem
{
public:
  PointLightRenderingSystem(const component::Manager& ecs, const Engine& engine, const SceneGraph& model,
                            const Materials& materials, VkCommandBuffer command_buffer);
  void operator()() const;

private:
  VkCommandBuffer                          command_buffer;
  const Engine&                            engine;
  const Materials&                         materials;
  const SceneGraph&                        model;
  const Components<Vec3>&                  positions;
  const Components<Vec4>&                  colors;
  const Components<component::PointLight>& point_lights;
};
#endif
