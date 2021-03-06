#include "game_render_entity.hh"
#include "engine/aligned_push_consts.hh"
#include "game.hh"
#include "player.hh"

namespace {

uint64_t filter_nodes_with_mesh(const ArrayView<Node>& nodes)
{
  uint64_t result = 0;
  for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.count); ++i)
    if (nodes[i].flags.mesh)
      result |= (uint64_t(1) << i);
  return result;
}

struct SkinningUbo
{
  Mat4x4 projection;
  Mat4x4 view;
  Mat4x4 model;
  Vec3   camera_position;
};

} // namespace

RenderEntityParams::RenderEntityParams(const Player& p)
    : projection(p.camera_projection)
    , view(p.camera_view)
    , camera_position(p.get_camera().position)
{
}

void render_pbr_entity_shadow(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                              const Game& game, VkCommandBuffer cmd, const int cascade_idx)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;

  struct Push
  {
    Mat4x4   model;
    uint32_t cascade_idx = 0;
  } push;

  push.cascade_idx = static_cast<uint32_t>(cascade_idx);

  for (uint32_t node_idx = 0; node_idx < static_cast<uint32_t>(scene_graph.nodes.count); ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      push.model           = entity.node_transforms[node_idx];

      vkCmdBindIndexBuffer(cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      AlignedPushConsts(cmd, engine.pipelines.shadowmap.layout).push(VK_SHADER_STAGE_VERTEX_BIT, push);
      vkCmdDrawIndexed(cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_pbr_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                       const RenderEntityParams& p)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;

  SkinningUbo ubo;

  ubo.projection      = p.projection;
  ubo.view            = p.view;
  ubo.camera_position = p.camera_position;

  for (uint32_t node_idx = 0; node_idx < static_cast<uint32_t>(scene_graph.nodes.count); ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];
      ubo.model            = entity.node_transforms[node_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      AlignedPushConsts(p.cmd, p.pipeline_layout).push(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, ubo);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_wireframe_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                             const RenderEntityParams& p)
{
  const uint64_t nodes_with_mesh_bitmap = filter_nodes_with_mesh(scene_graph.nodes);
  const uint64_t bitmap                 = entity.node_renderabilities & nodes_with_mesh_bitmap;

  for (uint32_t node_idx = 0; node_idx < static_cast<uint32_t>(scene_graph.nodes.count); ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int    mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh&  mesh     = scene_graph.meshes.data[mesh_idx];
      const Mat4x4 mvp      = p.projection * p.view * entity.node_transforms[node_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      AlignedPushConsts(p.cmd, p.pipeline_layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, mvp)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, p.color);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                   const RenderEntityParams& p)
{
  const uint64_t bitmap          = entity.node_renderabilities & filter_nodes_with_mesh(scene_graph.nodes);
  const Mat4x4   projection_view = p.projection * p.view;

  for (uint32_t node_idx = 0; node_idx < static_cast<uint32_t>(scene_graph.nodes.count); ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      const Mat4x4 calculated_mvp = projection_view * entity.node_transforms[node_idx];
      AlignedPushConsts(p.cmd, p.pipeline_layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, calculated_mvp)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, p.color);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}

void render_entity_skinned(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                           const RenderEntityParams& p)
{
  const uint64_t bitmap          = entity.node_renderabilities & filter_nodes_with_mesh(scene_graph.nodes);
  const Mat4x4   projection_view = p.projection * p.view;

  for (uint32_t node_idx = 0; node_idx < static_cast<uint32_t>(scene_graph.nodes.count); ++node_idx)
  {
    if (bitmap & (uint64_t(1) << node_idx))
    {
      const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
      const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

      vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
      vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);
      AlignedPushConsts(p.cmd, p.pipeline_layout)
          .push(VK_SHADER_STAGE_VERTEX_BIT, projection_view)
          .push(VK_SHADER_STAGE_FRAGMENT_BIT, p.color);
      vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
    }
  }
}
