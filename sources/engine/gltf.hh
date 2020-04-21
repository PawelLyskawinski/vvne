#pragma once

#include "engine.hh"
#include "math.hh"
#include "vtl/span.hh"
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
  Span<int> children = {};

  Quaternion rotation;
  Vec3       translation;
  Vec3       scale;
  Mat4x4     matrix;

  int mesh = 0;
  int skin = 0;

  struct Flags
  {
    bool children : 1;
    bool rotation : 1;
    bool translation : 1;
    bool scale : 1;
    bool matrix : 1;
    bool mesh : 1;
    bool skin : 1;
  };

  Flags flags = {};
};

struct Scene
{
  Span<int> nodes;
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
  Span<AnimationChannel> channels;
  Span<AnimationSampler> samplers;

  bool has_rotations;
  bool has_translations;
};

struct Skin
{
  Span<Mat4x4> inverse_bind_matrices;
  Span<int>    joints;
  int               skeleton;
};

struct SceneGraph
{
  Span<Material>  materials;
  Span<Mesh>      meshes;
  Span<Node>      nodes;
  Span<Scene>     scenes;
  Span<Animation> animations;
  Span<Skin>      skins;
};

SceneGraph loadGLB(Engine& engine, const char* path);
