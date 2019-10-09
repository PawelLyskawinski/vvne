#pragma once

#include "engine.hh"
#include "math.hh"
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
  ArrayView<int> children = {};

  Quaternion rotation;
  Vec3       translation;
  Vec3       scale;
  Mat4x4     matrix;

  int mesh = 0;
  int skin = 0;

  enum Property : uint64_t
  {
    Children    = uint64_t(1) << 0u,
    Rotation    = uint64_t(1) << 1u,
    Translation = uint64_t(1) << 2u,
    Scale       = uint64_t(1) << 3u,
    Matrix      = uint64_t(1) << 4u,
    Mesh        = uint64_t(1) << 5u,
    Skin        = uint64_t(1) << 6u
  };

  uint64_t flags = 0;
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

  [[nodiscard]] bool operator==(const Path& path) const { return path == target_path; }

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

  bool has_rotations;
  bool has_translations;
};

struct Skin
{
  ArrayView<Mat4x4> inverse_bind_matrices;
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
