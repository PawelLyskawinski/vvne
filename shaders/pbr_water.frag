#version 450

layout(push_constant) uniform Transformation
{
  mat4  projection;
  mat4  view;
  mat4  model;
  vec3  camPos;
  float time;
}
transformation;

// textures not used in case of water - the values have to be generated at runtime!
// layout(set = 0, binding = 0) uniform sampler2D pbr_material[5];

// texture ordering:
// 0.0 irradiance (cubemap)
// 0.1 prefiltered (cubemap)
// 1   BRDF lookup table (2D)
layout(set = 0, binding = 0) uniform samplerCube pbr_ibl_material[2];
layout(set = 0, binding = 1) uniform sampler2D brdf_lut;
layout(set = 1, binding = 0) uniform LightSourcesUbo
{
  vec4 positions[64];
  vec4 colors[64];
  int  count;
}
light_sources_ubo;
layout(set = 2, binding = 0) uniform sampler2D pbr_normal_map;

layout(location = 0) in vec4 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 getNormalFromMap()
{
  vec3 tangentNormal = texture(pbr_normal_map, inTexCoord).rgb * 2.0 - 1.0;

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

void main()
{
  vec3  albedo_color    = vec3(0.3, 0.3, 1.0);//texture(pbr_normal_map, inTexCoord).rgb;
  float metallic_color  = 0.9;
  float roughness_color = 0.1;
  vec3  emissive_color  = vec3(0.0);
  float ao_color        = 1.0;
  vec3  normal          = inNormal.xyz;//vec3(1.0, 0.0, 0.0);

  vec3 N = getNormalFromMap();
  vec3 V = normalize(transformation.camPos - inWorldPos);

  // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0
  // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)
  vec3 F0 = vec3(0.04);
  F0      = mix(F0, albedo_color, metallic_color);

  // reflectance equation
  vec3 Lo = vec3(0.0);
  for (int i = 0; i < light_sources_ubo.count; ++i)
  {
    // vec3 lightPosition = vec3(4.5, 5.0, 22.0);
    // vec3 lightColor    = vec3(100.0, 0.0, 0.0);

    vec3 lightPosition = light_sources_ubo.positions[i].xyz;
    vec3 lightColor    = light_sources_ubo.colors[i].xyz;

    // calculate per-light radiance
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

    // kS is equal to Fresnel
    vec3 kS = F;
    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    vec3 kD = vec3(1.0) - kS;
    // multiply kD by the inverse metalness such that only non-metals
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= 1.0 - metallic_color;

    // scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);

    // add to outgoing radiance Lo
    Lo += (kD * albedo_color / PI + specular) * radiance *
          NdotL; // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
  }

  vec3 F  = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness_color);
  vec3 kS = F;
  vec3 kD = 1.0 - kS;
  kD *= 1.0 - metallic_color;
  vec3        irradiance         = texture(pbr_ibl_material[0], N).rgb;
  vec3        diffuse            = irradiance * albedo_color;
  vec3        R                  = reflect(-V, N);
  const float MAX_REFLECTION_LOD = 4.0;
  vec3        prefilteredColor   = textureLod(pbr_ibl_material[1], R, roughness_color * MAX_REFLECTION_LOD).rgb;
  vec2        envBRDF            = texture(brdf_lut, vec2(max(dot(N, V), 0.0), roughness_color)).rg;
  vec3        specular           = prefilteredColor * (F * envBRDF.x + envBRDF.y);
  vec3        ambient            = (kD * diffuse + specular) * ao_color;
  vec3        color              = ambient + Lo;

  // HDR tonemapping
  color = color / (color + vec3(1.0));
  // gamma correction
  color = pow(color, vec3(1.0 / 2.2));
  color += emissive_color;

  outColor = vec4(color, 0.6);
}
