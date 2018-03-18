#include "cJSON.h"
#include "engine.hh"
#include "gltf.hh"
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_stdinc.h>

namespace gltf {

void Model::loadASCII(const char* path) noexcept
{
  SDL_RWops* ctx         = SDL_RWFromFile(path, "r");
  size_t     fileSize    = static_cast<size_t>(SDL_RWsize(ctx));
  char*      fileContent = static_cast<char*>(SDL_malloc(fileSize));
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

      accessor.bufferView    = cJSON_GetObjectItem(jsonAccessor, "bufferView")->valueint;
      accessor.componentType = cJSON_GetObjectItem(jsonAccessor, "componentType")->valueint;
      accessor.count         = cJSON_GetObjectItem(jsonAccessor, "count")->valueint;

      const char* type = cJSON_GetObjectItem(jsonAccessor, "type")->valuestring;
      if (0 == SDL_strcmp("SCALAR", type))
      {
        accessor.type = ACCESSOR_TYPE_SCALAR;
      }
      else if (0 == SDL_strcmp("VEC3", type))
      {
        accessor.type = ACCESSOR_TYPE_VEC3;
      }
      else if (0 == SDL_strcmp("VEC2", type))
      {
        accessor.type = ACCESSOR_TYPE_VEC2;
      }
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

      bufferView.buffer     = cJSON_GetObjectItem(jsonBufferView, "buffer")->valueint;
      bufferView.byteLength = cJSON_GetObjectItem(jsonBufferView, "byteLength")->valueint;
      bufferView.byteOffset = cJSON_GetObjectItem(jsonBufferView, "byteOffset")->valueint;
      bufferView.target     = cJSON_GetObjectItem(jsonBufferView, "target")->valueint;
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

      texture.sampler = cJSON_GetObjectItem(jsonTexture, "sampler")->valueint;
      texture.source  = cJSON_GetObjectItem(jsonTexture, "source")->valueint;
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

      node.mesh           = cJSON_GetObjectItem(jsonNode, "mesh")->valueint;
      cJSON* jsonRotation = cJSON_GetObjectItem(jsonNode, "rotation");

      for (int i = 0; i < 4; ++i)
        node.rotation[i] = static_cast<float>(cJSON_GetArrayItem(jsonRotation, i)->valuedouble);
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

        primitive.position_attrib = cJSON_GetObjectItem(attribs, "POSITION")->valueint;
        primitive.normal_attrib   = cJSON_GetObjectItem(attribs, "NORMAL")->valueint;
        primitive.texcoord_attrib = cJSON_GetObjectItem(attribs, "TEXCOORD_0")->valueint;
        primitive.indices         = cJSON_GetObjectItem(jsonPrimitive, "indices")->valueint;
        primitive.material        = cJSON_GetObjectItem(jsonPrimitive, "material")->valueint;
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

      cJSON* emissiveFactor = cJSON_GetObjectItem(jsonMaterial, "emissiveFactor");
      for (int i = 0; i < 3; ++i)
        material.emissiveFactor[i] = static_cast<float>(cJSON_GetArrayItem(emissiveFactor, i)->valuedouble);

      auto getIndex = [](cJSON* parent, const char* name) {
        return cJSON_GetObjectItem(cJSON_GetObjectItem(parent, name), "index")->valueint;
      };

      material.emissiveTextureIdx  = getIndex(jsonMaterial, "emissiveTexture");
      material.normalTextureIdx    = getIndex(jsonMaterial, "normalTexture");
      material.occlusionTextureIdx = getIndex(jsonMaterial, "occlusionTexture");

      cJSON* pbr                              = cJSON_GetObjectItem(jsonMaterial, "pbrMetallicRoughness");
      material.pbrBaseColorTextureIdx         = getIndex(pbr, "baseColorTexture");
      material.pbrMetallicRoughnessTextureIdx = getIndex(pbr, "metallicRoughnessTexture");
    }
  }

  char relativePath[256] = {};
  int  lastSlashIdx      = 0;
  for (int i = 0; i < SDL_strlen(path); ++i)
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

      SDL_Log("image filename: %s", filename);

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
  SDL_free(fileContent);
}

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
      SDL_Log("  [primitive] position_attrib: %d, normal_attrib: %d, texcoord_attrib: %d, indices: %d, material: %d",
              primitive.position_attrib, primitive.normal_attrib, primitive.texcoord_attrib, primitive.indices,
              primitive.material);
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
