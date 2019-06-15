#version 450

const float PI                 = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

const mat4 bias_mat = mat4(0.5, 0.0, 0.0, 0.0, //
                           0.0, 0.5, 0.0, 0.0, //
                           0.0, 0.0, 1.0, 0.0, //
                           0.5, 0.5, 0.0, 1.0  //
);

layout(constant_id = 0) const int SHADOW_MAP_CASCADE_COUNT = 4;

layout(push_constant) uniform PushConst
{
  mat4  projection;
  mat4  view;
  vec3  cam_pos;
  float adjustment;
  float time;
}
transformation;

layout(location = 0) in vec4 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inViewPos;
layout(location = 0) out vec4 outColor;

/*
void main()
{
  float r = (sin(push_const.time) + 1.0) / 2.0;
  float g = (cos(2.0 * push_const.time) + 1.0) / 2.0;
  float b = (sin(0.4 * push_const.time + 0.2) + 1.0) / 2.0;

  outColor = vec4(r, g, b, 1.0);
}
*/

layout(set = 1, binding = 0) uniform sampler2D pbr_material[5];

//
// Image Based Lighting Material (only textures)
//
// ordering:
// 0.0 irradiance cube
// 0.1 prefiltered cube
// 1   BRDF lookup table
//
layout(set = 2, binding = 0) uniform samplerCube pbr_ibl_material[2];
layout(set = 2, binding = 1) uniform sampler2D brdf_lut;

//
// Cascade Shadow Mapping Precomputation texture
//
layout(set = 3, binding = 0) uniform sampler2DArray shadow_map;

//
// Dynamic Lights for PBR
//
layout(set = 4, binding = 0) uniform LightSourcesUbo
{
  vec4 positions[64];
  vec4 colors[64];
  int  count;
}
light_sources_ubo;

//
// Global Light for shadow mapping
//
layout(set = 5, binding = 0) uniform CascadeShadowMappingUBO
{
  mat4 cascade_view_proj_mat[SHADOW_MAP_CASCADE_COUNT];
  vec4 cascade_splits;
}
cascade_shadow_mapping_ubo;

// -----------------------------------------------------------------------------
// Code
// -----------------------------------------------------------------------------

vec3 getNormalFromMap()
{
  vec3 tangentNormal = texture(pbr_material[4], inTexCoord).xyz * 2.0 - 1.0;

  vec3 Q1  = dFdx(inWorldPos);
  vec3 Q2  = dFdy(inWorldPos);
  vec2 st1 = dFdx(inTexCoord);
  vec2 st2 = dFdy(inTexCoord);

  vec3 N   = normalize(inNormal.rgb);
  vec3 T   = normalize(Q1 * st2.t - Q2 * st1.t);
  vec3 B   = -normalize(cross(N, T));
  mat3 TBN = mat3(T, B, N);

  return normalize(TBN * tangentNormal);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
  float a      = roughness * roughness;
  float a2     = a * a;
  float NdotH  = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float num   = a2;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom       = PI * denom * denom;

  return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
  float r = (roughness + 1.0);
  float k = (r * r) / 8.0;

  float num   = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2  = GeometrySchlickGGX(NdotV, roughness);
  float ggx1  = GeometrySchlickGGX(NdotL, roughness);

  return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float textureProj(vec4 P, vec2 off, uint cascade_idx)
{
  float shadow      = 1.2;
  float bias        = 0.005;
  vec4  shadowCoord = P / P.w;

  if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0)
  {
    float dist = texture(shadow_map, vec3(shadowCoord.st + off, cascade_idx)).r;
    if (shadowCoord.w > 0.0 && dist < shadowCoord.z)
    {
      shadow = 0.3;
    }
  }

  return shadow;
}

float filterPCF(vec4 sc, uint cascade_idx)
{
  ivec2 texDim = textureSize(shadow_map, 0).xy;
  float scale  = 0.75;
  float dx     = scale * 1.0 / float(texDim.x);
  float dy     = scale * 1.0 / float(texDim.y);

  float shadowFactor = 0.0;
  int   count        = 0;
  int   range        = 3;

  for (int x = -range; x <= range; x++)
  {
    for (int y = -range; y <= range; y++)
    {
      shadowFactor += textureProj(sc, vec2(dx * x, dy * y), cascade_idx);
      count++;
    }
  }

  return shadowFactor / count;
}

void main()
{
  vec3  albedo_color    = pow(texture(pbr_material[0], inTexCoord).rgb, vec3(2.2));
  float metallic_color  = texture(pbr_material[1], inTexCoord).b;
  float roughness_color = texture(pbr_material[1], inTexCoord).g;
  vec3  emissive_color  = texture(pbr_material[2], inTexCoord).rgb;
  float ao_color        = texture(pbr_material[3], inTexCoord).r;
  vec3  normal          = texture(pbr_material[4], inTexCoord).rgb;

  vec3 N = getNormalFromMap();
  vec3 V = normalize(transformation.cam_pos.xyz - inWorldPos);

  // dielectric like plastic have constant F0 value. Left for future reference.
  // vec3 F0 = vec3(0.04);
  vec3 F0 = mix(vec3(0.04), albedo_color, metallic_color);

  //
  // Solving Reflectance Equation
  //
  vec3 Lo = vec3(0.0);
  for (int i = 0; i < light_sources_ubo.count; ++i)
  {
    vec3 lightPosition = light_sources_ubo.positions[i].xyz;
    vec3 lightColor    = light_sources_ubo.colors[i].xyz;

    // per-light radiance
    vec3  L           = normalize(lightPosition - inWorldPos);
    vec3  H           = normalize(V + L);
    float distance    = length(lightPosition - inWorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3  radiance    = lightColor * attenuation;

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness_color);
    float G   = GeometrySmith(N, V, L, roughness_color);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  nominator   = NDF * G * F;
    float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; // 0.001 to prevent divide by zero.
    vec3  specular    = nominator / denominator;

    vec3 kS = F;

    // For energy conservation, the diffuse and specular light can't be above 1.0 (unless the surface emits light)
    // To preserve this relationship the diffuse component (kD) should equal 1.0 - kS
    vec3 kD = vec3(1.0) - kS;

    // Multiply kD by the inverse metalness such that only non-metals have diffuse lighting,
    // or a linear blend if partly metal (pure metals have no diffuse light).
    kD *= 1.0 - metallic_color;

    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo_color / PI + specular) * radiance * NdotL;
  }

  vec3 F  = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness_color);
  vec3 kS = F;
  vec3 kD = 1.0 - kS;
  kD *= 1.0 - metallic_color;

  //
  // Applying Image Based Reflection
  //
  vec3 irradiance       = texture(pbr_ibl_material[0], N).rgb;
  vec3 diffuse          = irradiance * albedo_color;
  vec3 R                = reflect(-V, N);
  vec3 prefilteredColor = textureLod(pbr_ibl_material[1], R, roughness_color * MAX_REFLECTION_LOD).rgb;
  vec2 envBRDF          = texture(brdf_lut, vec2(max(dot(N, V), 0.0), roughness_color)).rg;
  vec3 specular         = prefilteredColor * (F * envBRDF.x + envBRDF.y);
  vec3 ambient          = (kD * diffuse + specular) * ao_color;

  //
  // Applying Cascade Shadow Mapping
  //
  uint cascade_idx = 0;
  for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i)
  {
    if (inViewPos.z > cascade_shadow_mapping_ubo.cascade_splits[i])
    {
      cascade_idx = i + 1;
    }
  }

  vec4  shadow_coord = bias_mat * cascade_shadow_mapping_ubo.cascade_view_proj_mat[cascade_idx] * vec4(inWorldPos, 1.0);
  float shadow       = filterPCF(shadow_coord / shadow_coord.w, cascade_idx);

  //
  // Finalizing
  //
  vec3 color = (ambient * shadow) + Lo;
  color      = color / (color + vec3(1.0));
  color      = pow(color, vec3(1.0 / 2.2));
  color += emissive_color;

  outColor = vec4(color, 1.0);
}
