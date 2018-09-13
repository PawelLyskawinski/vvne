#include "game.hh"

namespace {

uint64_t filter_nodes_with_mesh(const Node nodes[], const int n)
{
  uint64_t result = 0;
  for (int i = 0; i < n; ++i)
    if (nodes[i].has(Node::Property::Mesh))
      result |= (1ULL << i);
  return result;
}

uint64_t filter_nodes_with_mesh(const ArrayView<Node>& nodes)
{
  return filter_nodes_with_mesh(nodes.data, nodes.count);
}

struct SkinningUbo
{
  mat4x4 projection;
  mat4x4 view;
  mat4x4 model;
  vec3   camera_position;
};

SkinningUbo to_skinning(RenderEntityParams& p, mat4x4 transform)
{
  SkinningUbo r = {};

  mat4x4_dup(r.projection, p.projection);
  mat4x4_dup(r.view, p.view);
  mat4x4_dup(r.model, transform);

  for (int i = 0; i < 3; ++i)
    r.camera_position[i] = p.camera_position[i];

  return r;
}

} // namespace

void render_pbr_entity_shadow(Entity entity, EntityComponentSystem& ecs, SceneGraph& scene_graph, Engine& engine,
                              Game& game, VkCommandBuffer cmd, int cascade_idx)
{
  uint64_t renderable_nodes_bitmap = ecs.node_renderabilities[entity.node_renderabilities];
  uint64_t nodes_with_mesh_bitmap  = filter_nodes_with_mesh(scene_graph.nodes);
  uint64_t bitmap                  = renderable_nodes_bitmap & nodes_with_mesh_bitmap;
  mat4x4*  transforms              = ecs.node_transforms[entity.node_transforms].transforms;

  struct Push
  {
    mat4x4   model;
    uint32_t cascade_idx;
  } push = {};

  push.cascade_idx = static_cast<uint32_t>(cascade_idx);

  for (int node_idx = 0; node_idx < 64; ++node_idx)
  {
    if (bitmap & (1ULL << node_idx))
    {
      int         mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      mat4x4_dup(push.model, transforms[node_idx]);

      vkCmdBindIndexBuffer(cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      vkCmdPushConstants(cmd, engine.shadowmap_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
      vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_pbr_entity(Entity entity, EntityComponentSystem& ecs, SceneGraph& scene_graph, Engine& engine,
                       RenderEntityParams& p)
{
  uint64_t renderable_nodes_bitmap = ecs.node_renderabilities[entity.node_renderabilities];
  uint64_t nodes_with_mesh_bitmap  = filter_nodes_with_mesh(scene_graph.nodes);
  uint64_t bitmap                  = renderable_nodes_bitmap & nodes_with_mesh_bitmap;
  mat4x4*  transforms              = ecs.node_transforms[entity.node_transforms].transforms;

  for (int node_idx = 0; node_idx < 64; ++node_idx)
  {
    if (bitmap & (1ULL << node_idx))
    {
      int         mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      SkinningUbo ubo      = to_skinning(p, transforms[node_idx]);

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(ubo), &ubo);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_entity(Entity entity, EntityComponentSystem& ecs, SceneGraph& scene_graph, Engine& engine,
                   RenderEntityParams& p)
{
  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, p.projection, p.view);

  const uint64_t bitmap =
      ecs.node_renderabilities[entity.node_renderabilities] & filter_nodes_with_mesh(scene_graph.nodes);
  mat4x4* transforms = ecs.node_transforms[entity.node_transforms].transforms;

  for (int node_idx = 0; node_idx < 64; ++node_idx)
  {
    if (bitmap & (1ULL << node_idx))
    {
      int         mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);

      mat4x4 calculated_mvp = {};
      mat4x4_mul(calculated_mvp, projection_view, transforms[node_idx]);

      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), calculated_mvp);
      vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), p.color);

      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}
