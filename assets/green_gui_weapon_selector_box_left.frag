#version 450

layout(push_constant) uniform Transformation
{
  layout(offset = 64) float time;
  float                     aspect_ratio;
  float                     transparency;
}
transformation;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec4 draw_straight_lines_border(vec4 input_color, vec4 color, vec2 uv, float x_ratio)
{
  const float bottom_line_width = 0.17;
  const float line_width        = 0.03;

  if ((uv.y < line_width) && (uv.x < 1.0f - ((color.a - 0.4) / 7.2)))
  {
    return color;
  }

  const float indent_marker_line_y = (-15.0 * uv.x) + 15.0 - (2.0 * (color.a - 0.4));

  if (uv.y > (1.0 - bottom_line_width))
  {
    if (uv.y < indent_marker_line_y)
    {
      return color;
    }
  }
  else if (((uv.y + line_width) > indent_marker_line_y) && ((uv.y) < indent_marker_line_y))
  {
    return color;
  }

  if (uv.x < (line_width * x_ratio))
  {
    return color;
  }

  return input_color;
}

void main()
{
  // const vec4  dark_green   = {55.0f / 255.0f, 134.0f / 255.0f, 104.0f / 255.0f, transformation.transparency};
  // const vec4  mid_green    = {95.0f / 255.0f, 174.0f / 255.0f, 144.0f / 255.0f, transformation.transparency};
  const vec4  light_green = {145.0f / 255.0f, 224.0f / 255.0f, 194.0f / 255.0f, transformation.transparency};
  const float x_ratio     = transformation.aspect_ratio;
  vec4        base_color  = {0.0, 0.0, 0.0, 0.0};

  base_color = draw_straight_lines_border(base_color, light_green, inUV, x_ratio);

  outColor = base_color;
}
