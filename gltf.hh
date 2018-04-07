#pragma once

#include "engine.hh"
#include <SDL2/SDL_stdinc.h>
#include <vulkan/vulkan.h>

namespace gltf {

template <typename T> struct ArrayView
{
  T*  elements;
  int n;

  size_t size() const noexcept
  {
    return n * sizeof(T);
  }

  T& operator[](int idx) noexcept
  {
    return elements[idx];
  }

  const T& operator[](int idx) const noexcept
  {
    return elements[idx];
  }

  T* begin() const noexcept
  {
    return elements;
  }

  T* end() const noexcept
  {
    return &elements[n];
  }
};

#define ACCESSOR_TYPE_SCALAR 0
#define ACCESSOR_TYPE_VEC2 1
#define ACCESSOR_TYPE_VEC3 2

#define ACCESSOR_COMPONENTTYPE_SINT8 5120
#define ACCESSOR_COMPONENTTYPE_UINT8 5121
#define ACCESSOR_COMPONENTTYPE_SINT16 5122
#define ACCESSOR_COMPONENTTYPE_UINT16 5123
#define ACCESSOR_COMPONENTTYPE_SINT32 5124
#define ACCESSOR_COMPONENTTYPE_UINT32 5125
#define ACCESSOR_COMPONENTTYPE_FLOAT 5126

struct Accessor
{
  enum Flag
  {
    hasBufferView    = (1 << 0),
    hasComponentType = (1 << 1),
    hasCount         = (1 << 2),
    hasType          = (1 << 3),
    hasByteOffset    = (1 << 4)
  };
  int flags;

  int bufferView;
  int componentType;
  int count;
  int type;
  int byteOffset;
};

struct BufferView
{
  enum Flag
  {
    hasBuffer     = (1 << 0),
    hasByteLength = (1 << 1),
    hasByteOffset = (1 << 2),
    hasTarget     = (1 << 3),
    hasByteStride = (1 << 4)
  };
  int flags;

  int buffer;
  int byteLength;
  int byteOffset;
  int target;
  int byteStride;
};

struct Texture
{
  enum Flag
  {
    hasSampler = (1 << 0),
    hasSource  = (1 << 1)
  };
  int flags;

  int sampler;
  int source;
};

struct Node
{
  enum Flag
  {
    hasMesh     = (1 << 0),
    hasChild    = (1 << 1),
    hasRotation = (1 << 2),
    hasMatrix   = (1 << 3)
  };
  int flags;

  int   mesh;
  int   child;
  float rotation[4];
  float matrix[16];
};

struct Primitive
{
  enum Flag
  {
    hasPositionAttrib = (1 << 0),
    hasNormalAttrib   = (1 << 1),
    hasTexcoordAttrib = (1 << 2),
    hasIndices        = (1 << 3),
    hasMaterial       = (1 << 4),
  };
  int flags;

  int position_attrib;
  int normal_attrib;
  int texcoord_attrib;
  int indices;
  int material;
};

struct Mesh
{
  ArrayView<Primitive> primitives;
};

struct Material
{
  enum Flag
  {
    hasEmissiveFactor                 = (1 << 0),
    hasEmissiveTextureIdx             = (1 << 1),
    hasNormalTextureIdx               = (1 << 2),
    hasOcclusionTextureIdx            = (1 << 3),
    hasPbrBaseColorTextureIdx         = (1 << 4),
    hasPbrMetallicRoughnessTextureIdx = (1 << 5)
  };
  int flags;

  float emissiveFactor[3];
  int   emissiveTextureIdx;
  int   normalTextureIdx;
  int   occlusionTextureIdx;
  int   pbrBaseColorTextureIdx;
  int   pbrMetallicRoughnessTextureIdx;
};

struct SmallString
{
  char data[128];
};

struct Buffer
{
  int         size;
  SmallString path;
};

struct Model
{
  int      usedMemory;
  uint8_t* memory;

  ArrayView<Accessor>    accessors;
  ArrayView<BufferView>  bufferViews;
  ArrayView<Buffer>      buffers;
  ArrayView<SmallString> images;
  ArrayView<Texture>     textures;
  ArrayView<Node>        nodes;
  ArrayView<Mesh>        meshes;
  ArrayView<Material>    materials;

  void debugDump() noexcept;
  void loadASCII(Engine::DoubleEndedStack& stack, const char* path) noexcept;
};

struct RenderableModel
{
  VkDeviceSize indices_offset;
  VkDeviceSize vertices_offset;
  VkIndexType  indices_type;
  uint32_t     indices_count;

  int albedo_texture_idx;
  int metal_roughness_texture_idx;
  int emissive_texture_idx;
  int AO_texture_idx;
  int normal_texture_idx;

  void construct(Engine& engine, const Model& model) noexcept;
};

} // namespace gltf
