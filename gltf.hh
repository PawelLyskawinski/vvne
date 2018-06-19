#pragma once

#include "engine.hh"
#include "linmath.h"
#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

template <typename T> struct ArrayView
{
  T*  data;
  int count;

  T& operator[](int idx)
  {
    return data[idx];
  }

  const T& operator[](int idx) const
  {
    return data[idx];
  }

  T* begin()
  {
    return data;
  }

  T* end()
  {
    return &data[count];
  }

  T* begin() const
  {
    return data;
  }

  T* end() const
  {
    return &data[count];
  }
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

  quat   rotation;
  vec3   translation;
  vec3   scale;
  mat4x4 matrix;

  int mesh;
  int skin;

  enum Property
  {
    Children,
    Rotation,
    Translation,
    Scale,
    Matrix,
    Mesh,
    Skin
  };

  bool has(Property property) const
  {
    return static_cast<bool>(flags & (1 << property));
  }

  void set(Property property)
  {
    flags |= (1 << property);
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
    Translation,
    Scale
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

struct Skin
{
  ArrayView<mat4x4> inverse_bind_matrices;
  ArrayView<int>    joints;
  int               skeleton;
};

struct SceneGraph
{
  ArrayView<Material>  materials;
  ArrayView<Mesh>      meshes;
  ArrayView<Node>      nodes;
  ArrayView<Scene>     scenes;
  ArrayView<Animation> animations;
  ArrayView<Skin>      skins;
};

namespace gltf {

struct RenderableModel
{
  SceneGraph scene_graph;

  bool    animation_enabled;
  float   animation_start_time;
  vec3    animation_translations[32];
  quat    animation_rotations[32];
  vec3    animation_scales[32];
  uint8_t animation_properties[32];

  void loadGLB(Engine& engine, const char* path) noexcept;
  void renderColored(Engine& engine, VkCommandBuffer cmd, mat4x4 projection, mat4x4 view, mat4x4 world_transform,
                     vec3 color, Engine::SimpleRendering::Passes pass, VkDeviceSize joint_ubo_offset,
                     vec3 camera_position) noexcept;
  void renderRaw(Engine& engine, VkCommandBuffer cmd) const noexcept;
};

} // namespace gltf
