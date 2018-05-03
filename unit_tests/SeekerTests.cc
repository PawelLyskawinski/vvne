#include <SDL2/SDL_log.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_assert.h>

namespace {

size_t find_substring_idx(const char* big_string, const size_t big_string_length, const char* small_string)
{
  size_t result              = 0;
  size_t small_string_length = SDL_strlen(small_string);

  for (size_t i = 0; i < (big_string_length - small_string_length); ++i)
  {
    if (0 == SDL_memcmp(&big_string[i], small_string, small_string_length))
    {
      result = i;
      break;
    }
  }

  return result;
}

bool is_open_bracket(char in)
{
  return ('{' == in) or ('[' == in);
}

bool is_closing_bracket(char in)
{
  return ('}' == in) or (']' == in);
}

struct Seeker
{
  const char* data;
  size_t      length;

  Seeker node(const char* name) const
  {
    const size_t name_length = SDL_strlen(name);
    const char*  iter        = data;

    while ('{' != *iter)
      ++iter;
    ++iter;

    int  open_brackets = 1;
    char character     = *iter;

    while (1 <= open_brackets)
    {
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }
      else if ((1 == open_brackets) and ('"' == character))
      {
        if (0 == SDL_memcmp(&iter[1], name, name_length))
        {
          return {iter, length - (iter - data)};
        }
      }

      ++iter;
      character = *iter;
    }

    return *this;
  }

  bool has(const char* name) const
  {
    const char* iter = data;
    while ('{' != *iter)
      ++iter;
    ++iter;

    int open_brackets = 1;
    while ((1 <= open_brackets) and (iter != &data[length]))
    {
      char character = *iter;
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }

      ++iter;
    }

    size_t sublen = (iter - data);
    return (0 != find_substring_idx(data, sublen, name));
  }

  Seeker idx(const int desired_array_element) const
  {
    Seeker result{};

    const char* iter = data;
    while ('[' != *iter)
      ++iter;
    ++iter;

    if (0 != desired_array_element)
    {
      int open_brackets = 1;
      int array_element = 0;

      while (array_element != desired_array_element)
      {
        char character = *iter;
        if (is_open_bracket(character))
        {
          open_brackets += 1;
        }
        else if (is_closing_bracket(character))
        {
          open_brackets -= 1;
        }
        else if ((1 == open_brackets) and (',' == character))
        {
          array_element += 1;
        }

        ++iter;
      }
    }

    result.data   = iter;
    result.length = length - (iter - data);

    return result;
  }

  int idx_integer(const int desired_array_element) const
  {
    Seeker element = idx(desired_array_element);
    long   result  = SDL_strtol(element.data, nullptr, 10);
    return static_cast<int>(result);
  }

  float idx_float(const int desired_array_element) const
  {
    Seeker      element  = idx(desired_array_element);
    const char* adjusted = &element.data[1];
    double      result   = SDL_strtod(adjusted, nullptr);
    return static_cast<float>(result);
  }

  int elements_count() const
  {
    const char* iter = data;
    while ('[' != *iter)
      ++iter;
    ++iter;

    int  result        = 1;
    int  open_brackets = 1;
    char character     = *iter;

    while (1 <= open_brackets)
    {
      if (is_open_bracket(character))
      {
        open_brackets += 1;
      }
      else if (is_closing_bracket(character))
      {
        open_brackets -= 1;
      }
      else if ((1 == open_brackets) and (',' == character))
      {
        result += 1;
      }

      ++iter;
      character = *iter;
    }

    return result;
  }

  int integer(const char* name) const
  {
    const size_t idx = find_substring_idx(data, length, name);

    const char* iter = &data[idx];
    while (':' != *iter)
      ++iter;
    ++iter;

    long result = SDL_strtol(iter, nullptr, 10);
    return static_cast<int>(result);
  }
};

} // namespace

int main() {
  //
  // MATERIALS SUITE
  //
  {
    const char* sample =
        R"({"materials":[{"emissiveFactor":[ 1.0, 1.0, 1.0 ],"emissiveTexture":{"index":2 },"name":"Material_MR","normalTexture":{"index":4},"occlusionTexture":{"index":3},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}}}],)";
    // -------------------------------------

    Seeker document{sample, static_cast<uint32_t>(SDL_strlen(sample))};
    Seeker materials = document.node("materials");
    SDL_assert(1 == materials.elements_count());

    Seeker material = materials.idx(0);
    SDL_assert(material.has("emissiveFactor"));
    SDL_assert(!material.has("emissiveFactory"));
    SDL_assert(!material.has("abcd"));
    SDL_assert(!material.has("1234"));

    auto float_compare = [](float lhs, float rhs) -> bool {
      constexpr float epsilon = 0.00001f;
      return SDL_fabsf(lhs - rhs) < epsilon;
    };

    for (int i = 0; i < 3; ++i)
      SDL_assert(float_compare(1.0f, material.node("emissiveFactor").idx_float(i)));

    SDL_assert(material.has("emissiveTexture"));
    SDL_assert(2 == material.node("emissiveTexture").integer("index"));
    SDL_assert(material.has("normalTexture"));
    SDL_assert(4 == material.node("normalTexture").integer("index"));

    Seeker pbrMetallicRoughness      = material.node("pbrMetallicRoughness");
    int    metal_roughness_image_idx = pbrMetallicRoughness.node("metallicRoughnessTexture").integer("index");
    int    occlusion_image_idx       = material.node("occlusionTexture").integer("index");
    int    emissive_image_idx        = material.node("emissiveTexture").integer("index");

    SDL_assert(1 == metal_roughness_image_idx);
    SDL_assert(2 == emissive_image_idx);
    SDL_assert(3 == occlusion_image_idx);
  }

  //
  // MATERIALS SUITE
  //
  {
    const char* sample =
        R"({"meshes":[{"primitives":[{"attributes":{"NORMAL":1,"POSITION":2},"indices":0,"mode":4,"material":0}],"name":"inner_box"},{"primitives":[{"attributes":{"NORMAL":4,"POSITION":5},"indices":3,"mode":4,"material":1}],"name":"outer_box"}],"animations":[)";
    // -------------------------------------

    Seeker document{sample, static_cast<uint32_t>(SDL_strlen(sample))};
    SDL_assert(2 == document.node("meshes").elements_count());
  }

  //
  // NODES SUITE
  //
  {
    const char* sample =
        R"({"asset":{"generator":"COLLADA2GLTF","version":"2.0"},"scene":0,"scenes":[{"nodes":[3,0]}],"nodes":[{"children":[1],"rotation":[-0.0,-0.0,-0.0,-1.0]},{"children":[2]},{"mesh":0,"rotation":[-0.0,-0.0,-0.0,-1.0]},{"mesh":1}],"meshes":[{"primitives":[{"attributes":{"NORMAL":1,"POSITION":2},"indices":0,"mode":4,"material":0}],"name":"inner_box"},{"primitives":[{"attributes":{"NORMAL":4,"POSITION":5},"indices":3,"mode":4,"material":1}],"name":"outer_box"}],"animations":[{"channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}},{"sampler":1,"target":{"node":0,"path":"translation"}}],"samplers":[{"input":6,"interpolation":"LINEAR","output":7},{"input":8,"interpolation":"LINEAR","output":9}]}],"accessors":[{"bufferView":0,"byteOffset":0,"componentType":5123,"count":186,"max":[95],"min":[0],"type":"SCALAR"},{"bufferView":1,"byteOffset":0,"componentType":5126,"count":96,"max":[1.0,1.0,1.0],"min":[-1.0,-1.0,-1.0],"type":"VEC3"},{"bufferView":1,"byteOffset":1152,"componentType":5126,"count":96,"max":[0.33504000306129458,0.5,0.33504000306129458],"min":[-0.33504000306129458,-0.5,-0.33504000306129458],"type":"VEC3"},{"bufferView":0,"byteOffset":372,"componentType":5123,"count":576,"max":[223],"min":[0],"type":"SCALAR"},{"bufferView":1,"byteOffset":2304,"componentType":5126,"count":224,"max":[1.0,1.0,1.0],"min":[-1.0,-1.0,-1.0],"type":"VEC3"},{"bufferView":1,"byteOffset":4992,"componentType":5126,"count":224,"max":[0.5,0.5,0.5],"min":[-0.5,-0.5,-0.5],"type":"VEC3"},{"bufferView":2,"byteOffset":0,"componentType":5126,"count":2,"max":[2.5],"min":[1.25],"type":"SCALAR"},{"bufferView":3,"byteOffset":0,"componentType":5126,"count":2,"max":[1.0,0.0,0.0,4.4896593387466768e-11],"min":[-0.0,0.0,0.0,-1.0],"type":"VEC4"},{"bufferView":2,"byteOffset":8,"componentType":5126,"count":4,"max":[3.708329916000366],"min":[0.0],"type":"SCALAR"},{"bufferView":4,"byteOffset":0,"componentType":5126,"count":4,"max":[0.0,2.5199999809265138,0.0],"min":[0.0,0.0,0.0],"type":"VEC3"}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.800000011920929,0.4159420132637024,0.7952920198440552,1.0],"metallicFactor":0.0},"name":"inner"},{"pbrMetallicRoughness":{"baseColorFactor":[0.3016040027141571,0.5335419774055481,0.800000011920929,1.0],"metallicFactor":0.0},"name":"outer"}],"bufferViews":[{"buffer":0,"byteOffset":7784,"byteLength":1524,"target":34963},{"buffer":0,"byteOffset":80,"byteLength":7680,"byteStride":12,"target":34962},{"buffer":0,"byteOffset":7760,"byteLength":24},{"buffer":0,"byteOffset":0,"byteLength":32},{"buffer":0,"byteOffset":32,"byteLength":48}],"buffers":[{"byteLength":9308}]})";

    Seeker document{sample, static_cast<uint32_t>(SDL_strlen(sample))};
    SDL_assert(4 == document.node("nodes").elements_count());
    SDL_assert(1 == document.node("nodes").idx(0).node("children").idx_integer(0));
  }

  {
    const char* sample =
        R"({"accessors":[{"bufferView":0,"componentType":5123,"count":46356,"max":[14555],"min":[0],"type":"SCALAR"},{"bufferView":1,"componentType":5126,"count":14556,"max":[0.9424954056739807,0.8128451108932495,0.900973916053772],"min":[-0.9474585652351379,-1.18715500831604,-0.9009949564933777],"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":14556,"max":[1,1,1],"min":[-1,-1,-1],"type":"VEC3"},{"bufferView":3,"componentType":5126,"count":14556,"max":[0.9999759793281555,1.998665988445282],"min":[0.002448640065267682,1.0005531199858524],"type":"VEC2"}],"asset":{"generator":"Khronos Blender glTF 2.0 exporter","version":"2.0"},"bufferViews":[{"buffer":0,"byteLength":92712,"byteOffset":0,"target":34963},{"buffer":0,"byteLength":174672,"byteOffset":92712,"target":34962},{"buffer":0,"byteLength":174672,"byteOffset":267384,"target":34962},{"buffer":0,"byteLength":116448,"byteOffset":442056,"target":34962},{"buffer":0,"byteOffset":558504,"byteLength":935629},{"buffer":0,"byteOffset":1494136,"byteLength":1300661},{"buffer":0,"byteOffset":2794800,"byteLength":97499},{"buffer":0,"byteOffset":2892300,"byteLength":361678},{"buffer":0,"byteOffset":3253980,"byteLength":517757}],"buffers":[{"byteLength":3771740}],"images":[{"bufferView":4,"mimeType":"image/jpeg"},{"bufferView":5,"mimeType":"image/jpeg"},{"bufferView":6,"mimeType":"image/jpeg"},{"bufferView":7,"mimeType":"image/jpeg"},{"bufferView":8,"mimeType":"image/jpeg"}],"materials":[{"emissiveFactor":[1,1,1],"emissiveTexture":{"index":2},"name":"Material_MR","normalTexture":{"index":4},"occlusionTexture":{"index":3},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}}}],"meshes":[{"name":"mesh_helmet_LP_13930damagedHelmet","primitives":[{"attributes":{"NORMAL":2,"POSITION":1,"TEXCOORD_0":3},"indices":0,"material":0}]}],"nodes":[{"mesh":0,"name":"node_damagedHelmet_-6514","rotation":[0.7071068286895752,0,0,0.7071068286895752]}],"samplers":[{}],"scene":0,"scenes":[{"name":"Scene","nodes":[0]}],"textures":[{"sampler":0,"source":0},{"sampler":0,"source":1},{"sampler":0,"source":2},{"sampler":0,"source":3},{"sampler":0,"source":4}]})";

    Seeker document{sample, static_cast<uint32_t>(SDL_strlen(sample))};
    Seeker images       = document.node("images");
    int    images_count = document.node("images").elements_count();

    Seeker material_json                   = document.node("materials").idx(0);
    Seeker pbrMetallicRoughness            = material_json.node("pbrMetallicRoughness");
    int    albedo_image_idx                = pbrMetallicRoughness.node("baseColorTexture").integer("index");
    int    albedo_buffer_view_idx          = images.idx(albedo_image_idx).integer("bufferView");
    int    metal_roughness_image_idx       = pbrMetallicRoughness.node("metallicRoughnessTexture").integer("index");
    int    metal_roughness_buffer_view_idx = images.idx(metal_roughness_image_idx).integer("bufferView");
    int    emissive_image_idx              = material_json.node("emissiveTexture").integer("index");
  }

  {
    const char* sample =
        R"({"children":[1],"rotation":[-0.0,-0.0,-0.0,-1.0]},{"children":[2]},{"mesh":0,"rotation":[-0.0,-0.0,-0.0,-1.0]},{"mesh":1}],"meshes":[{"primitives":[{"attributes":{"NORMAL":1,"POSITION":2},"indices":0,"mode":4,"material":0}],"name":"inner_box"},{"primitives":[{"attributes":{"NORMAL":4,"POSITION":5},"indices":3,"mode":4,"material":1}],"name":"outer_box"}],"animations":[{"channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}},{"sampler":1,"target":{"node":0,"path":"translation"}}],"samplers":[{"input":6,"interpolation":"LINEAR","output":7},{"input":8,"interpolation":"LINEAR","output":9}]}])";

    Seeker document{sample, static_cast<uint32_t>(SDL_strlen(sample))};
    Seeker node_json = document.node("nodes").idx(0);
    SDL_assert(false == node_json.has("mesh"));
  }

  return 0;
}