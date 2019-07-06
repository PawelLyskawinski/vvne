#include "game.hh"
#include <SDL2/SDL_assert.h>

namespace {

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

class BitOffsetView
{
public:
  explicit BitOffsetView(uint64_t bitfield)
      : bitfield(bitfield)
  {
  }

  class Iterator
  {
  public:
    explicit Iterator(uint64_t bitfield, uint64_t offset)
        : bitfield(bitfield)
        , offset(offset)
    {
    }

    Iterator& operator++()
    {
      if (bitfield >> 1)
      {
        uint64_t shift_value = 1;
        while (0 == (bitfield & (uint64_t(1) << shift_value)))
        {
          shift_value += 1;
        }
        bitfield >>= shift_value;
        offset += shift_value;
      }
      else
      {
        bitfield = uint64_t(0);
      }
      return *this;
    }

    int  operator*() const { return static_cast<int>(offset); }
    bool operator!=(const Iterator& rhs) { return bitfield != rhs.bitfield; }

  private:
    uint64_t bitfield;
    uint64_t offset;
  };

  Iterator begin() const
  {
    // finding start of collection
    uint64_t i = 0;
    while (0 == (bitfield & (uint64_t(1) << i)))
    {
      i += 1;
    }
    return Iterator(bitfield >> i, i);
  }

  Iterator end() const { return Iterator(0, 0); }

private:
  uint64_t bitfield;
};

class NodesWithMeshView : public BitOffsetView
{
public:
  NodesWithMeshView(const SimpleEntity& entity, const SceneGraph& scene_graph)
      : BitOffsetView(get_renderable_entities_bitmap(entity, scene_graph))
  {
  }

private:
  static uint64_t get_renderable_entities_bitmap(const SimpleEntity& entity, const SceneGraph& scene_graph)
  {
    uint64_t bitmap = 0;
    for (int i = 0; i < scene_graph.nodes.count; ++i)
      if (scene_graph.nodes[i].flags & Node::Property::Mesh)
        bitmap |= (uint64_t(1) << i);
    return bitmap & entity.node_renderabilities;
  }
};

} // namespace

void render_pbr_entity_shadow(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                              const Game& game, VkCommandBuffer cmd, const int cascade_idx)
{
  struct Push
  {
    mat4x4   model;
    uint32_t cascade_idx;
  } push = {};

  push.cascade_idx = static_cast<uint32_t>(cascade_idx);

  for (int node_idx : NodesWithMeshView(entity, scene_graph))
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

void render_pbr_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                       const RenderEntityParams& p)
{
  SkinningUbo ubo = {};
  SDL_memcpy(ubo.projection, p.projection, sizeof(mat4x4));
  SDL_memcpy(ubo.view, p.view, sizeof(mat4x4));
  SDL_memcpy(ubo.camera_position, p.camera_position, sizeof(vec3));

  for (int node_idx : NodesWithMeshView(entity, scene_graph))
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

void render_wireframe_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                             const RenderEntityParams& p)
{
  for (int node_idx : NodesWithMeshView(entity, scene_graph))
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

void render_entity(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                   const RenderEntityParams& p)
{
  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, p.projection, p.view);

  for (int node_idx : NodesWithMeshView(entity, scene_graph))
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

void render_entity_skinned(const SimpleEntity& entity, const SceneGraph& scene_graph, const Engine& engine,
                           const RenderEntityParams& p)
{
  mat4x4 projection_view = {};
  mat4x4_mul(projection_view, p.projection, p.view);

  for (int node_idx : NodesWithMeshView(entity, scene_graph))
  {
    const int   mesh_idx = scene_graph.nodes.data[node_idx].mesh;
    const Mesh& mesh     = scene_graph.meshes.data[mesh_idx];

    vkCmdBindIndexBuffer(p.cmd, engine.gpu_device_local_memory_buffer, mesh.indices_offset, mesh.indices_type);
    vkCmdBindVertexBuffers(p.cmd, 0, 1, &engine.gpu_device_local_memory_buffer, &mesh.vertices_offset);

    vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4x4), projection_view);
    vkCmdPushConstants(p.cmd, p.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(mat4x4), sizeof(vec3), p.color);
    vkCmdDrawIndexed(p.cmd, mesh.indices_count, 1, 0, 0, 0);
  }
}
