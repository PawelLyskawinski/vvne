#include "terrain_as_a_function.hh"

uint32_t tesellated_patches_nonindexed_calculate_count(uint32_t layers)
{
  // ## observations ##
  // - corner vertices don't overlap
  // - edge vertices only overlap for two patches
  // - all other vertices overlap for four patches

  const uint32_t vertices_on_edge   = (2 * layers) + 1;
  const uint32_t total_unique_count = vertices_on_edge * vertices_on_edge;
  const uint32_t non_overlapping    = 4;
  const uint32_t edge_vertices      = vertices_on_edge - 2;
  const uint32_t four_overlaps      = total_unique_count - non_overlapping - edge_vertices;

  return non_overlapping + (2 * edge_vertices) + (4 * four_overlaps);
}

namespace {

struct VertexBuilder
{
  TerrainVertex build(const Vec2 cursor) const
  {
    TerrainVertex v = {
        .position = Vec3(cursor.x, 0.0f, cursor.y),
        .normal   = Vec3(0.0f, -1.0f, 0.0f),
        .uv =
            Vec2(SDL_fabsf(static_cast<float>(top_left_point.x - cursor.x)) / static_cast<float>(vertices_on_edge),
                 SDL_fabsf(static_cast<float>(top_left_point.y - cursor.y)) / static_cast<float>(vertices_on_edge))};

    return v;
  }

  Vec2     top_left_point   = {};
  uint32_t vertices_on_edge = {};
};

struct Cursor
{
  explicit Cursor(const Vec2& in)
      :cursor(in)
  {
  }

  Cursor(float initial_x, float initial_y)
  {
    cursor.x = initial_x;
    cursor.y = initial_y;
  }

  Cursor operator+(const Cursor& rhs) const { return Cursor(cursor + rhs.cursor); }
  const Vec2& as_table() { return cursor; }
  float&      x() { return cursor.x; }
  float&      y() { return cursor.y; }

  Vec2 cursor = {};
};

} // namespace

void tesellated_patches_nonindexed_generate(const uint32_t layers, const float patch_dimention, TerrainVertex verts[])
{
  // - Since we aren't indexing, we'll have to do the squere patches one by one
  // - vertices are in distance 1.0 to any neighbour. Any scaling will be done during rendering

  VertexBuilder builder;
  builder.top_left_point.x = -patch_dimention * layers;
  builder.top_left_point.y = patch_dimention * layers;
  builder.vertices_on_edge  = (2 * layers) + 1;

  uint32_t added_verts_counter = 0;

  for (uint32_t layer = 1; layer <= layers; ++layer)
  {
    // We start in top left corner. We'll have to calculate it's offset first.
    Cursor         cursor(-patch_dimention * layer, patch_dimention * layer);
    const uint32_t steps_per_wall = (2 * layer) - 1;

    for (uint32_t rectangle_side = 0; rectangle_side < 4; ++rectangle_side)
    {
      for (uint32_t step = 0; step < steps_per_wall; ++step)
      {
        switch (rectangle_side)
        {
        case 0:
          cursor.y() -= patch_dimention;
          break;
        case 1:
          cursor.x() += patch_dimention;
          break;
        case 2:
          cursor.y() += patch_dimention;
          break;
        case 3:
          cursor.x() -= patch_dimention;
          break;
        default:
          break;
        }

        verts[added_verts_counter++] = builder.build(cursor.as_table());
        verts[added_verts_counter++] = builder.build((cursor + Cursor(0.0f, -patch_dimention)).as_table());
        verts[added_verts_counter++] = builder.build((cursor + Cursor(patch_dimention, -patch_dimention)).as_table());
        verts[added_verts_counter++] = builder.build((cursor + Cursor(patch_dimention, 0.0f)).as_table());
      }
    }
  }
}
