#pragma once

#include "allocators.hh"
#include "engine.hh"
#include "linmath.h"
#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

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

  enum Property : uint64_t
  {
    Children    = (1 << 0),
    Rotation    = (1 << 1),
    Translation = (1 << 2),
    Scale       = (1 << 3),
    Matrix      = (1 << 4),
    Mesh        = (1 << 5),
    Skin        = (1 << 6)
  };

  uint64_t flags;
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
