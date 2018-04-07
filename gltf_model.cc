#include "cJSON.h"
#include "engine.hh"
#include "gltf.hh"
#include <SDL2/SDL_log.h>

namespace {

struct Loader
{
  Loader(int* flags, cJSON* json)
      : flags(flags)
      , json(json)
  {
  }

  void loadInt(int flag, int* dst, const char* name)
  {
    if (cJSON_HasObjectItem(json, name))
    {
      *flags |= flag;
      *dst = cJSON_GetObjectItem(json, name)->valueint;
    }
  }

  void loadStringAsInt(int flag, int* dst, const char* name, int (*convert)(const char*))
  {
    if (cJSON_HasObjectItem(json, name))
    {
      *flags |= flag;
      *dst = convert(cJSON_GetObjectItem(json, name)->valuestring);
    }
  }

  void loadVector(int flag, float* dst, const char* name, int length)
  {
    if (cJSON_HasObjectItem(json, name))
    {
      *flags |= flag;
      cJSON* vec = cJSON_GetObjectItem(json, name);
      for (int i = 0; i < length; ++i)
        dst[i] = static_cast<float>(cJSON_GetArrayItem(vec, i)->valuedouble);
    }
  }

  void loadIntFromIndexChild(int flag, int* dst, const char* name)
  {
    if (cJSON_HasObjectItem(json, name))
    {
      const char* idxname = "index";
      cJSON*      child   = cJSON_GetObjectItem(json, name);
      if (cJSON_HasObjectItem(child, idxname))
      {
        *flags |= flag;
        *dst = cJSON_GetObjectItem(child, idxname)->valueint;
      }
    }
  }

  int*   flags;
  cJSON* json;
};

} // namespace

namespace gltf {

void Model::loadASCII(Engine::DoubleEndedStack& stack, const char* path) noexcept
{
  SDL_RWops* ctx         = SDL_RWFromFile(path, "r");
  size_t     fileSize    = static_cast<size_t>(SDL_RWsize(ctx));
  char*      fileContent = stack.allocate_back<char>(fileSize);
  SDL_RWread(ctx, fileContent, sizeof(char), fileSize);
  SDL_RWclose(ctx);

  cJSON* document = cJSON_Parse(fileContent);

  {
    cJSON* jsonAccessors = cJSON_GetObjectItem(document, "accessors");
    accessors.n          = cJSON_GetArraySize(jsonAccessors);

    accessors.elements = reinterpret_cast<Accessor*>(&memory[usedMemory]);
    usedMemory += accessors.size();

    for (int accessorIdx = 0; accessorIdx < accessors.n; ++accessorIdx)
    {
      cJSON*    jsonAccessor = cJSON_GetArrayItem(jsonAccessors, accessorIdx);
      Accessor& accessor     = accessors[accessorIdx];

      auto typeConvert = [](const char* in) -> int {
        int result = ACCESSOR_TYPE_VEC3;
        if (0 == SDL_strcmp("SCALAR", in))
        {
          result = ACCESSOR_TYPE_SCALAR;
        }
        else if (0 == SDL_strcmp("VEC3", in))
        {
          result = ACCESSOR_TYPE_VEC3;
        }
        else if (0 == SDL_strcmp("VEC2", in))
        {
          result = ACCESSOR_TYPE_VEC2;
        }
        return result;
      };

      Loader loader(&accessor.flags, jsonAccessor);
      loader.loadInt(Accessor::Flag ::hasBufferView, &accessor.bufferView, "bufferView");
      loader.loadInt(Accessor::Flag ::hasComponentType, &accessor.componentType, "componentType");
      loader.loadInt(Accessor::Flag::hasCount, &accessor.count, "count");
      loader.loadInt(Accessor::Flag::hasByteOffset, &accessor.byteOffset, "byteOffset");
      loader.loadStringAsInt(Accessor::Flag::hasType, &accessor.type, "type", typeConvert);
    }
  }

  {
    cJSON* jsonBufferViews = cJSON_GetObjectItem(document, "bufferViews");
    bufferViews.n          = cJSON_GetArraySize(jsonBufferViews);

    bufferViews.elements = reinterpret_cast<BufferView*>(&memory[usedMemory]);
    usedMemory += bufferViews.size();

    for (int bufferViewIdx = 0; bufferViewIdx < bufferViews.n; ++bufferViewIdx)
    {
      cJSON*      jsonBufferView = cJSON_GetArrayItem(jsonBufferViews, bufferViewIdx);
      BufferView& bufferView     = bufferViews[bufferViewIdx];

      Loader loader(&bufferView.flags, jsonBufferView);
      loader.loadInt(BufferView::Flag::hasBuffer, &bufferView.buffer, "buffer");
      loader.loadInt(BufferView::Flag::hasByteLength, &bufferView.byteLength, "byteLength");
      loader.loadInt(BufferView::Flag::hasByteOffset, &bufferView.byteOffset, "byteOffset");
      loader.loadInt(BufferView::Flag::hasTarget, &bufferView.target, "target");
      loader.loadInt(BufferView::Flag::hasByteStride, &bufferView.byteStride, "byteStride");
    }
  }

  {
    cJSON* jsonTextures = cJSON_GetObjectItem(document, "textures");
    textures.n          = cJSON_GetArraySize(jsonTextures);

    textures.elements = reinterpret_cast<Texture*>(&memory[usedMemory]);
    usedMemory += textures.size();

    for (int textureIdx = 0; textureIdx < textures.n; ++textureIdx)
    {
      cJSON*   jsonTexture = cJSON_GetArrayItem(jsonTextures, textureIdx);
      Texture& texture     = textures[textureIdx];

      Loader loader(&texture.flags, jsonTexture);
      loader.loadInt(Texture::Flag::hasSampler, &texture.sampler, "sampler");
      loader.loadInt(Texture::Flag::hasSource, &texture.source, "source");
    }
  }

  {
    cJSON* jsonNodes = cJSON_GetObjectItem(document, "nodes");
    nodes.n          = cJSON_GetArraySize(jsonNodes);

    nodes.elements = reinterpret_cast<Node*>(&memory[usedMemory]);
    usedMemory += nodes.size();

    for (int nodeIdx = 0; nodeIdx < nodes.n; ++nodeIdx)
    {
      cJSON* jsonNode = cJSON_GetArrayItem(jsonNodes, nodeIdx);
      Node&  node     = nodes[nodeIdx];

      Loader loader(&node.flags, jsonNode);

      loader.loadInt(Node::Flag::hasMesh, &node.mesh, "mesh");
      loader.loadVector(Node::Flag::hasRotation, node.rotation, "rotation", 4);
    }
  }

  {
    cJSON* jsonMeshes = cJSON_GetObjectItem(document, "meshes");
    meshes.n          = cJSON_GetArraySize(jsonMeshes);

    meshes.elements = reinterpret_cast<Mesh*>(&memory[usedMemory]);
    usedMemory += meshes.size();

    for (int meshIdx = 0; meshIdx < meshes.n; ++meshIdx)
    {
      cJSON* jsonMesh = cJSON_GetArrayItem(jsonMeshes, meshIdx);
      Mesh&  mesh     = meshes[meshIdx];

      cJSON* jsonPrimitives = cJSON_GetObjectItem(jsonMesh, "primitives");
      mesh.primitives.n     = cJSON_GetArraySize(jsonPrimitives);

      mesh.primitives.elements = reinterpret_cast<Primitive*>(&memory[usedMemory]);
      usedMemory += mesh.primitives.size();

      for (int primitiveIdx = 0; primitiveIdx < mesh.primitives.n; ++primitiveIdx)
      {
        cJSON* jsonPrimitive = cJSON_GetArrayItem(jsonPrimitives, primitiveIdx);
        cJSON* attribs       = cJSON_GetObjectItem(jsonPrimitive, "attributes");

        Primitive& primitive = mesh.primitives[primitiveIdx];

        {
          Loader loader(&primitive.flags, attribs);
          loader.loadInt(Primitive::Flag::hasPositionAttrib, &primitive.position_attrib, "POSITION");
          loader.loadInt(Primitive::Flag::hasNormalAttrib, &primitive.normal_attrib, "NORMAL");
          loader.loadInt(Primitive::Flag::hasTexcoordAttrib, &primitive.texcoord_attrib, "TEXCOORD_0");
        }

        {
          Loader loader(&primitive.flags, jsonPrimitive);
          loader.loadInt(Primitive::Flag::hasIndices, &primitive.indices, "indices");
          loader.loadInt(Primitive::Flag::hasMaterial, &primitive.material, "material");
        }
      }
    }
  }

  {
    cJSON* jsonMaterials = cJSON_GetObjectItem(document, "materials");
    materials.n          = cJSON_GetArraySize(jsonMaterials);

    materials.elements = reinterpret_cast<Material*>(&memory[usedMemory]);
    usedMemory += materials.size();

    for (int materialIdx = 0; materialIdx < materials.n; ++materialIdx)
    {
      cJSON*    jsonMaterial = cJSON_GetArrayItem(jsonMaterials, materialIdx);
      Material& material     = materials[materialIdx];

      {
        Loader loader(&material.flags, jsonMaterial);
        loader.loadVector(Material::Flag::hasEmissiveFactor, material.emissiveFactor, "emissiveFactor", 3);
        loader.loadIntFromIndexChild(Material::Flag::hasEmissiveTextureIdx, &material.emissiveTextureIdx,
                                     "emissiveTexture");
        loader.loadIntFromIndexChild(Material::Flag::hasNormalTextureIdx, &material.normalTextureIdx, "normalTexture");
        loader.loadIntFromIndexChild(Material::Flag::hasOcclusionTextureIdx, &material.occlusionTextureIdx,
                                     "occlusionTexture");
      }

      if (cJSON_HasObjectItem(jsonMaterial, "pbrMetallicRoughness"))
      {
        cJSON* pbr = cJSON_GetObjectItem(jsonMaterial, "pbrMetallicRoughness");
        Loader loader(&material.flags, pbr);
        loader.loadIntFromIndexChild(Material::Flag::hasPbrBaseColorTextureIdx, &material.pbrBaseColorTextureIdx,
                                     "baseColorTexture");
        loader.loadIntFromIndexChild(Material::Flag::hasPbrMetallicRoughnessTextureIdx,
                                     &material.pbrMetallicRoughnessTextureIdx, "metallicRoughnessTexture");
      }
    }
  }

  char relativePath[256] = {};
  size_t  lastSlashIdx      = 0;
  for (size_t i = 0; i < SDL_strlen(path); ++i)
    if ('/' == path[i])
      lastSlashIdx = i;
  SDL_memcpy(relativePath, path, lastSlashIdx + 1);
  relativePath[lastSlashIdx + 1] = '\0';

  {
    cJSON* jsonImages = cJSON_GetObjectItem(document, "images");
    images.n          = cJSON_GetArraySize(jsonImages);

    images.elements = reinterpret_cast<SmallString*>(&memory[usedMemory]);
    usedMemory += images.size();

    for (int imageIdx = 0; imageIdx < images.n; ++imageIdx)
    {
      cJSON*      image    = cJSON_GetArrayItem(jsonImages, imageIdx);
      const char* filename = cJSON_GetObjectItem(image, "uri")->valuestring;

      SmallString& smallString = images[imageIdx];
      SDL_strlcpy(&smallString.data[0], relativePath, 128);
      SDL_strlcpy(&smallString.data[lastSlashIdx + 1], filename, 64);
    }
  }

  {
    cJSON* jsonBuffers = cJSON_GetObjectItem(document, "buffers");
    buffers.n          = cJSON_GetArraySize(jsonBuffers);

    buffers.elements = reinterpret_cast<Buffer*>(&memory[usedMemory]);
    usedMemory += images.size();

    for (int bufferIdx = 0; bufferIdx < buffers.n; ++bufferIdx)
    {
      cJSON*      jsonBuffer = cJSON_GetArrayItem(jsonBuffers, bufferIdx);
      const char* filename   = cJSON_GetObjectItem(jsonBuffer, "uri")->valuestring;

      Buffer& buffer = buffers[bufferIdx];
      SDL_strlcpy(buffer.path.data, relativePath, 128);
      SDL_strlcpy(&buffer.path.data[lastSlashIdx + 1], filename, 64);
      buffer.size = cJSON_GetObjectItem(jsonBuffer, "byteLength")->valueint;
    }
  }

  cJSON_Delete(document);
} // namespace gltf

void Model::debugDump() noexcept
{
  for (const auto& accessor : accessors)
  {
    SDL_Log("[accessor] count: %d, type: %d, bufferView: %d", accessor.count, accessor.type, accessor.bufferView);
  }

  for (const auto& bufferView : bufferViews)
  {
    SDL_Log("[bufferview] buffer: %d, byteLength: %d, byteOffset: %d, target: %d", bufferView.buffer,
            bufferView.byteLength, bufferView.byteOffset, bufferView.target);
  }

  for (const auto& texture : textures)
  {
    SDL_Log("[texture] sampler: %d, source: %d", texture.sampler, texture.source);
  }

  for (const auto& node : nodes)
  {
    SDL_Log("[node] mesh: %d, rotation: [%.2f, %.2f, %.2f, %.2f]", node.mesh, node.rotation[0], node.rotation[1],
            node.rotation[2], node.rotation[3]);
  }

  for (const auto& mesh : meshes)
  {
    SDL_Log("[mesh] primitives: %d", mesh.primitives.n);
    for (const auto& primitive : mesh.primitives)
    {
      SDL_Log("  [primitive] -- begin (flags: %d)", primitive.flags);
      if (primitive.flags & Primitive::Flag::hasPositionAttrib)
        SDL_Log("  position_attrib: %d", primitive.position_attrib);
      if (primitive.flags & Primitive::Flag::hasNormalAttrib)
        SDL_Log("  normal_attrib: %d", primitive.normal_attrib);
      if (primitive.flags & Primitive::Flag::hasTexcoordAttrib)
        SDL_Log("  texcoord_attrib: %d", primitive.texcoord_attrib);
      if (primitive.flags & Primitive::Flag::hasIndices)
        SDL_Log("  indices: %d", primitive.indices);
      if (primitive.flags & Primitive::Flag::hasMaterial)
        SDL_Log("  material: %d", primitive.material);
      SDL_Log("  [primitive] -- end");
    }
  }

  for (const auto& material : materials)
  {
    SDL_Log(
        "[material] emissiveFactor: [%.2f, %.2f, %.2f], emissive: %d, normal: %d, occlusion: %d, pbrBC: %d, pbrMR: %d",
        material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2], material.emissiveTextureIdx,
        material.normalTextureIdx, material.occlusionTextureIdx, material.pbrBaseColorTextureIdx,
        material.pbrMetallicRoughnessTextureIdx);
  }

  for (const auto& image : images)
  {
    SDL_Log("[image] uri: %s", image.data);
  }

  for (const auto& buffer : buffers)
  {
    SDL_Log("[buffer] size: %d, uri: %s", buffer.size, buffer.path.data);
  }
}

} // namespace gltf
