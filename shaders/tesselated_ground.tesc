#version 450

layout(push_constant) uniform PushConst
{
  mat4  projection;
  mat4  view;
  mat4  model;
  float adjustment;
}
push_const;

layout(set = 0, binding = 0) uniform UBO { vec4 frustum_planes[6]; }
ubo;

layout(location = 0) in vec4 inNormal[];
layout(location = 1) in vec2 inTexCoord[];

layout(location = 0) out vec4 outNormal[4];
layout(location = 1) out vec2 outTexCoord[4];
layout(vertices = 4) out;

layout(constant_id = 0) const float tessellatedEdgeSize  = 20.0f;
layout(constant_id = 1) const float tessellationFactor   = 0.01f;
layout(constant_id = 2) const float frustum_check_radius = 1.0f;
layout(constant_id = 3) const float y_scale              = 0.05f;
layout(constant_id = 4) const float y_offset             = 0.1f;

float screenSpaceTessFactor(vec4 p0, vec4 p1)
{
  // Calculate edge mid point
  vec4 midPoint = 0.5 * (p0 + p1);
  // Sphere radius as distance between the control points
  float radius = distance(p0, p1) / 0.5;

  // View space
  vec4 v0 = push_const.view * push_const.model * midPoint;

  // Project into clip space
  vec4 clip0 = (push_const.projection * (v0 - vec4(radius, vec3(0.0))));
  vec4 clip1 = (push_const.projection * (v0 + vec4(radius, vec3(0.0))));

  // Get normalized device coordinates
  clip0 /= clip0.w;
  clip1 /= clip1.w;

  // @TODO set those values right
  vec2 viewportDim = vec2(1900.0, 1200.0);

  // Convert to viewport coordinates
  clip0.xy *= viewportDim;
  clip1.xy *= viewportDim;

  // Return the tessellation factor based on the screen size
  // given by the distance of the two edge control points in screen space
  // and a reference (min.) tessellation size for the edge set by the application
  return clamp(distance(clip0, clip1) / tessellatedEdgeSize * tessellationFactor, 1.0, 64.0);
}

bool frustum_check(vec4 pos)
{
  // vec4 pos = gl_in[gl_InvocationID].gl_Position;
  pos.y -= y_scale * (cos(push_const.adjustment * pos.x) + cos(push_const.adjustment * pos.y));
  pos.y -= y_offset;

  for (int i = 0; i < 6; i++)
    if (dot(pos, ubo.frustum_planes[i]) + frustum_check_radius < 0.0)
      return false;
  return true;
}

void main()
{
  if (0 == gl_InvocationID)
  {
    if (frustum_check(gl_in[0].gl_Position) || frustum_check(gl_in[1].gl_Position)||
        frustum_check(gl_in[2].gl_Position) || frustum_check(gl_in[3].gl_Position))
    {
      gl_TessLevelOuter[0] = screenSpaceTessFactor(gl_in[3].gl_Position, gl_in[0].gl_Position);
      gl_TessLevelOuter[1] = screenSpaceTessFactor(gl_in[0].gl_Position, gl_in[1].gl_Position);
      gl_TessLevelOuter[2] = screenSpaceTessFactor(gl_in[1].gl_Position, gl_in[2].gl_Position);
      gl_TessLevelOuter[3] = screenSpaceTessFactor(gl_in[2].gl_Position, gl_in[3].gl_Position);
      gl_TessLevelInner[0] = mix(gl_TessLevelOuter[0], gl_TessLevelOuter[3], 0.5);
      gl_TessLevelInner[1] = mix(gl_TessLevelOuter[2], gl_TessLevelOuter[1], 0.5);
    }
    else
    {
      gl_TessLevelInner[0] = 0.1f;
      gl_TessLevelInner[1] = 0.1f;
      gl_TessLevelOuter[0] = 0.1f;
      gl_TessLevelOuter[1] = 0.1f;
      gl_TessLevelOuter[2] = 0.1f;
      gl_TessLevelOuter[3] = 0.1f;
    }
  }

  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
  outNormal[gl_InvocationID]          = inNormal[gl_InvocationID];
  outTexCoord[gl_InvocationID]        = inTexCoord[gl_InvocationID];
}
