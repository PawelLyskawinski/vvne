#pragma once

#include "ecs.hh"
#include "game.hh"

void render_pbr_entity_shadow(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                              const Game& game, VkCommandBuffer cmd, const int cascade_idx);

void render_pbr_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                       const RenderEntityParams& p);

void render_wireframe_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                             const RenderEntityParams& p);

void render_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                   const RenderEntityParams& p);
