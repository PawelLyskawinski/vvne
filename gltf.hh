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

  bool empty() const
  {
    return 0 == count;
  }
};

struct Material
{
  Texture albedo_texture;
  Texture metal_roughness_texture;
  Texture emissive_texture;
  Texture AO_texture;
  Texture normal_texture;
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
  enum class Interpolation
  {
    Linear,
    Step,
    CubicSpline
  };

  // todo: very naive (but fastest to implement) approach. This should be in form of buffer, buffer views and accessors
  float         time_frame[2];
  int           keyframes_count;
  float*        times;
  float*        values;
  Interpolation interpolation;
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

SceneGraph loadGLB(Engine& engine, const char* path);
