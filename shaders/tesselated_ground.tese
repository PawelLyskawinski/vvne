#version 450

layout(push_constant) uniform PushConst
{
  mat4  projection;
  mat4  view;
  vec3  cam_pos;
  float adjustment;
  float time;
}
push_const;

layout(quads, equal_spacing, cw) in;

layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inTexCoord[];

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outViewPos;

layout(constant_id = 0) const float y_scale  = 0.05f;
layout(constant_id = 1) const float y_offset = 0.1f;

float displacement(vec2 pos)
{
  float r = 0.0;
  r -= y_scale * (cos(push_const.adjustment * pos.x) + cos(push_const.adjustment * pos.y));
  r -= y_offset;
  return r;
}

vec3 calculate_normal(vec2 pos)
{
  const float split_distance = 0.1f;

  vec2 pos_extra_1 = pos - vec2(0.0, split_distance);
  vec2 pos_extra_2 = pos + vec2(0.0, split_distance);
  vec2 pos_extra_3 = pos - vec2(split_distance, 0.0);
  vec2 pos_extra_4 = pos + vec2(split_distance, 0.0);

  float h_0 = displacement(pos);
  float h_1 = displacement(pos_extra_1);
  float h_2 = displacement(pos_extra_2);
  float h_3 = displacement(pos_extra_3);
  float h_4 = displacement(pos_extra_4);

  h_1 -= h_0;
  h_2 -= h_0;
  h_3 -= h_0;
  h_4 -= h_0;

  vec3 v1 = vec3(pos_extra_1.x, h_1, pos_extra_1.y);
  vec3 v2 = vec3(pos_extra_2.x, h_2, pos_extra_2.y);
  vec3 v3 = vec3(pos_extra_3.x, h_3, pos_extra_3.y);
  vec3 v4 = vec3(pos_extra_4.x, h_4, pos_extra_4.y);

  vec3 diff_1 = v1 - v2;
  vec3 diff_2 = v3 - v4;

  vec3 cross_prod = cross(diff_1, diff_2);
  return normalize(-cross_prod);
}

void main()
{
  // Interpolate UV coordinates
  vec2 uv1    = mix(inTexCoord[0], inTexCoord[1], gl_TessCoord.x);
  vec2 uv2    = mix(inTexCoord[3], inTexCoord[2], gl_TessCoord.x);
  outTexCoord = mix(uv1, uv2, gl_TessCoord.y);

  vec3 n1   = mix(inNormal[0], inNormal[1], gl_TessCoord.x);
  vec3 n2   = mix(inNormal[3], inNormal[2], gl_TessCoord.x);
  // outNormal = vec4(mix(n1, n2, gl_TessCoord.y), 1.0f);

  // Interpolate positions
  vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
  vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
  vec4 pos  = mix(pos1, pos2, gl_TessCoord.y);

  // calculate function normal somehow
  outNormal = vec4(calculate_normal(pos.xz), 1.0);

  // Displace
  pos.y += displacement(pos.xz);
  // pos.z = -1.5f * (cos(pos.x) + cos(pos.y));
  // pos.y -= textureLod(displacementMap, outUV, 0.0).r * ubo.displacementFactor;

  gl_Position = push_const.projection * push_const.view  * pos;
 // outWorldPos = gl_Position.xyz;

  // Calculate vectors for lighting based on tessellated position
  outWorldPos = pos.xyz;
  outViewPos  = gl_Position.xyz;
}
