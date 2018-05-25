#include <math.h>
#include <stdio.h>

void calculate_n_shaped_area_lines_uv_coordinates(int top_offset, int side_offset, int bottom_offset,
                                                  int max_distance_from_line, int scene_width, int scene_height,
                                                  int line_points[12])
{
  int vertical_line_length   = scene_height - top_offset - bottom_offset;
  int horizontal_line_length = scene_width - 2 * (side_offset + max_distance_from_line);

  // bottom left
  {
    int* pt = &line_points[0];
    pt[0]   = side_offset + max_distance_from_line;
    pt[1]   = top_offset + vertical_line_length;
  }

  // top left
  {
    int* pt = &line_points[2];
    pt[0]   = side_offset + max_distance_from_line;
    pt[1]   = top_offset;
  }

  // center left
  {
    int* pt = &line_points[4];
    pt[0]   = side_offset + (2 * max_distance_from_line);
    pt[1]   = top_offset + max_distance_from_line;
  }

  // center right
  {
    int* pt = &line_points[6];
    pt[0]   = side_offset + (2 * max_distance_from_line) + horizontal_line_length;
    pt[1]   = top_offset + max_distance_from_line;
  }

  // top right
  {
    int* pt = &line_points[8];
    pt[0]   = scene_width - (side_offset + max_distance_from_line);
    pt[1]   = top_offset;
  }

  // bottom right
  {
    int* pt = &line_points[10];
    pt[0]   = scene_width - (side_offset + max_distance_from_line);
    pt[1]   = top_offset + vertical_line_length;
  }
}

int main()
{
  int result[12] = {};
  calculate_n_shaped_area_lines_uv_coordinates(10, 10, 10, 10, 500, 300, result);

  for (int i = 0; i < 6; ++i)
  {
    printf("%d %d\n", result[2 * i], result[2 * i + 1]);
  }

  return 0;
}