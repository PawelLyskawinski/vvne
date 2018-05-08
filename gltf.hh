#pragma once

#include "engine.hh"
#include "linmath.h"
#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

// todo: work on better interface
template <typename T> struct ArrayView
{
  T*  data;
  int count;
};

struct Material
{
  int albedo_texture_idx;
  int metal_roughness_texture_idx;
  int emissive_texture_idx;
  int AO_texture_idx;
  int normal_texture_idx;
};

struct Mesh
{
  VkDeviceSize indices_offset;
  VkDeviceSize vertices_offset;
  VkIndexType  indices_type;
  uint32_t     indices_count;
  int          material;
};

class Node
{
public:
  ArrayView<int> children;
  vec4           rotation;
  int            mesh;

  enum
  {
    ChildrenBit = 1 << 0,
    RotationBit = 1 << 1,
    MeshBit     = 1 << 2
  };

  bool has(unsigned bit) const
  {
    return static_cast<bool>(flags & bit);
  }

  void set(int bit)
  {
    flags |= bit;
  }

private:
  unsigned flags;
};

struct Scene
{
  ArrayView<int> nodes;
};

struct AnimationChannel
{
  enum class Path
  {
    Rotation,
    Translation
  };

  int  sampler_idx;
  int  target_node_idx;
  Path target_path;
};

struct AnimationSampler
{
  // todo: very naive (but fastest to implement) approach. This should be in form of buffer, buffer views and accessors
  float  time_frame[2];
  int    keyframes_count;
  float* times;
  float* values;
};

struct Animation
{
  ArrayView<AnimationChannel> channels;
  ArrayView<AnimationSampler> samplers;
};

struct SceneGraph
{
  ArrayView<Material>  materials;
  ArrayView<Mesh>      meshes;
  ArrayView<Node>      nodes;
  ArrayView<Scene>     scenes;
  ArrayView<Animation> animations;
};

namespace gltf {

struct MVP
{
  float projection[4][4];
  float view[4][4];
  float model[4][4];
};

struct RenderableModel
{
  SceneGraph scene_graph;

  bool  animation_enabled;
  float animation_start_time;
  vec4  animation_translations[32];
  quat  animation_rotations[32];

  void loadGLB(Engine& engine, const char* path) noexcept;
  void render(Engine& engine, VkCommandBuffer cmd, MVP& mvp) const noexcept;
  void renderColored(Engine& engine, VkCommandBuffer cmd, mat4x4 projection, mat4x4 view, vec4 global_position,
                     quat global_orientation, vec3 model_scale, vec3 color) noexcept;
  void renderRaw(Engine& engine, VkCommandBuffer cmd) const noexcept;
};

} // namespace gltf
