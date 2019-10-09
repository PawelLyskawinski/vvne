#pragma once

#include "engine/math.hh"
#include <vulkan/vulkan_core.h>

struct Game;
struct Engine;
struct Player;
struct SimpleEntity;
struct SceneGraph;

struct RenderEntityParams
{
  RenderEntityParams() = default;
  explicit RenderEntityParams(const Player& player);

  VkCommandBuffer  cmd = VK_NULL_HANDLE;
  Mat4x4           projection;
  Mat4x4           view;
  Vec3             camera_position;
  Vec3             color;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
};

void render_pbr_entity_shadow(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                              const Game& game, VkCommandBuffer cmd, int cascade_idx);

void render_pbr_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                       const RenderEntityParams& p);

void render_wireframe_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                             const RenderEntityParams& p);

void render_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                   const RenderEntityParams& p);

void render_entity_skinned(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                           const RenderEntityParams& p);
