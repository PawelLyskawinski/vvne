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
  int bufferView;
  int componentType;
  int count;
  int type;
};

struct BufferView
{
  int buffer;
  int byteLength;
  int byteOffset;
  int target;
};

struct Texture
{
  int sampler;
  int source;
};

struct Node
{
  int   mesh;
  float rotation[4];
};

struct Primitive
{
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
  void loadASCII(const char* path) noexcept;
};

struct RenderableModel
{
  VkDeviceMemory device_memory;
  VkBuffer       device_buffer;

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
  void teardown(const Engine& engine) noexcept;
};

} // namespace gltf
