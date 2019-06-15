#version 450

layout(push_constant) uniform PushConst
{
  mat4  projection;
  mat4  view;
  mat4  model;
  float adjustment;
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

void main()
{
  // Interpolate UV coordinates
  vec2 uv1    = mix(inTexCoord[0], inTexCoord[1], gl_TessCoord.x);
  vec2 uv2    = mix(inTexCoord[3], inTexCoord[2], gl_TessCoord.x);
  outTexCoord = mix(uv1, uv2, gl_TessCoord.y);

  vec3 n1   = mix(inNormal[0], inNormal[1], gl_TessCoord.x);
  vec3 n2   = mix(inNormal[3], inNormal[2], gl_TessCoord.x);
  outNormal = vec4(mix(n1, n2, gl_TessCoord.y), 1.0f);

  // Interpolate positions
  vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
  vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
  vec4 pos  = mix(pos1, pos2, gl_TessCoord.y);

  // Displace
  pos.y -= y_scale * (cos(push_const.adjustment * pos.x) + cos(push_const.adjustment * pos.y));
  pos.y -= y_offset;
  // pos.z = -1.5f * (cos(pos.x) + cos(pos.y));
  // pos.y -= textureLod(displacementMap, outUV, 0.0).r * ubo.displacementFactor;

  // Perspective projection
  gl_Position = push_const.projection * push_const.view * push_const.model * pos;

  // Calculate vectors for lighting based on tessellated position
  outWorldPos = vec3(push_const.model * pos);
  outViewPos  = pos.xyz;
}
