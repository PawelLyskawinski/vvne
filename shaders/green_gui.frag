#version 450

layout(push_constant) uniform Transformation
{
  layout(offset = 64) float time;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec4 draw_circle(vec4 input_color, vec2 uv, float dim, float length, vec4 overlay_color)
{
  vec2  center = vec2(0.5, 0.5);
  float dist   = distance(center, uv);

  if ((dist < dim) && (dist > (dim - length)))
    return overlay_color;
  return input_color;
}

vec4 draw_lines(vec4 input_color, vec2 uv, vec4 overlay_color)
{
  // center vertical line
  if ((uv.x < 0.502) && (uv.x > 0.498) && (uv.y > 0.05) && (uv.y < 0.95))
    return overlay_color;

  // left line
  if ((uv.x < 0.5) && (uv.x > 0.12))
  {
    float line_y = (0.6 * uv.x) + 0.2;
    float top    = line_y + 0.005;
    float bottom = line_y - 0.005;

    if ((uv.y < top) && (uv.y) > bottom)
      return overlay_color;
  }

  // right line
  if ((uv.x > 0.5) && (uv.x < 0.88))
  {
    float line_y = 1.0 - (0.6 * uv.x) - 0.2;
    float top    = line_y + 0.005;
    float bottom = line_y - 0.005;

    if ((uv.y < top) && (uv.y) > bottom)
      return overlay_color;
  }

  return input_color;
}

vec4 draw_background_rects(vec4 input_color, vec2 uv, vec4 overlay_color, float center_sphere_dim)
{
  if (center_sphere_dim < distance(uv, vec2(0.5, 0.5)))
  {
    const float bottom = 0.35;
    const float top    = 0.65;
    if (((uv.x > bottom) && (uv.x < top)) || ((uv.y > bottom) && (uv.y < top)))
      return input_color;

    return overlay_color;
  }

  return input_color;
}

void main()
{
  const float transparency = 0.75;
  const vec4  dark_green   = {55.0f / 255.0f, 134.0f / 255.0f, 104.0f / 255.0f, transparency};
  const vec4  mid_green    = {95.0f / 255.0f, 174.0f / 255.0f, 144.0f / 255.0f, transparency};
  const vec4  light_green  = {145.0f / 255.0f, 224.0f / 255.0f, 194.0f / 255.0f, transparency};
  vec4        base_color   = vec4(0.0, 0.0, 0.0, 0.0);

  base_color = draw_circle(base_color, inUV.st, 0.45, 0.45, dark_green);
  base_color = draw_circle(base_color, inUV.st, 0.36, 0.19, mid_green);

  base_color = draw_circle(base_color, inUV.st, 0.17, 0.01, light_green);
  base_color = draw_circle(base_color, inUV.st, 0.36, 0.01, light_green);
  base_color = draw_circle(base_color, inUV.st, 0.45, 0.01, light_green);

  base_color = draw_lines(base_color, inUV.st, light_green);
  base_color = draw_background_rects(base_color, inUV.st, mid_green, 0.47);

  outColor = base_color;
}
