#include "game.hh"
#include <SDL2/SDL_assert.h>

namespace {

uint64_t filter_nodes_with_mesh(const ArrayView<Node>& nodes)
{
  uint64_t result = 0;
  for (int i = 0; i < nodes.count; ++i)
    if (nodes[i].flags & Node::Property::Mesh)
      result |= (uint64_t(1) << i);
  return result;
}

struct SkinningUbo
{
  mat4x4 projection;
  mat4x4 view;
  mat4x4 model;
  vec3   camera_position;
};

void multiply(mat4x4 result, const mat4x4 lhs, const mat4x4 rhs) { mat4x4_mul(result, lhs, rhs); }

template <typename... Args> void multiply(mat4x4 result, const mat4x4 lhs, const mat4x4 rhs, Args... args)
{
  mat4x4 tmp = {};
  mat4x4_mul(tmp, lhs, rhs);
  multiply(result, tmp, args...);
}

} // namespace

void render_pbr_entity_shadow(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                              const Game& game, VkCommandBuffer cmd, const int cascade_idx)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;

  struct Push
  {
    mat4x4   model;
    uint32_t cascade_idx;
  } push = {};

  push.cascade_idx = static_cast<uint32_t>(cascade_idx);

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      SDL_memcpy(push.model, entity.node_transforms[node_idx], sizeof(mat4x4));

      vkCmdBindIndexBuffer(cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      vkCmdPushConstants(cmd, engine.pipelines.shadowmap.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
      vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_pbr_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                       const RenderEntityParams& p)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;
  SkinningUbo    ubo                    = {};

  SDL_memcpy(ubo.projection, p.projection, sizeof(mat4x4));
  SDL_memcpy(ubo.view, p.view, sizeof(mat4x4));
  SDL_memcpy(ubo.camera_position, p.camera_position, sizeof(vec3));

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      SDL_memcpy(ubo.model, entity.node_transforms[node_idx], sizeof(mat4x4));

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(ubo), &ubo);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_wireframe_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                             const RenderEntityParams& p)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      mat4x4      mvp      = {};
      multiply(mvp, p.projection, p.view, entity.node_transforms[node_idx]);

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), mvp);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mvp), sizeof(vec3), p.color);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                   const RenderEntityParams& p)
{
  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, p.projection, p.view);

  const uint64_t bitmap = entity.node_renderabilities & filter_nodes_with_mesh(scene_graph.nodes);

  for (int node_idx = 0; node_idx < scene_graph.nodes.count; ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);

      mat4x4 calculated_mvp = {};
      mat4x4_mul(calculated_mvp, projection_view, entity.node_transforms[node_idx]);

      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), calculated_mvp);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), p.color);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}
