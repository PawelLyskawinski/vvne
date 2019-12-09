#include "cascade_shadow_mapping.hh"
#include "engine_constants.hh"
#include <algorithm>

// CASCADE SHADOW MAPPING --------------------------------------------------------------------------------------------
// Based on:
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp
// -------------------------------------------------------------------------------------------------------------------

void recalculate_cascade_view_proj_matrices(Mat4x4* cascade_view_proj_mat, float* cascade_split_depths,
                                            Mat4x4 camera_projection, Mat4x4 camera_view, Vec3 light_source_position)
{
  constexpr float cascade_split_lambda = 0.95f;
  constexpr float near_clip            = 0.001f;
  constexpr float far_clip             = 500.0f;
  constexpr float clip_range           = far_clip - near_clip;
  constexpr float min_z                = near_clip;
  constexpr float max_z                = near_clip + clip_range;
  constexpr float range                = max_z - min_z;
  constexpr float ratio                = max_z / min_z;

  //
  // This calculates the distances between frustums. For example:
  // near:      0.1
  // far:    1000.0
  // splits: 0.013, 0.034, 0.132, 1.000
  //
  float cascade_splits[SHADOWMAP_CASCADE_COUNT] = {};
  for (uint32_t i = 0; i < SHADOWMAP_CASCADE_COUNT; i++)
  {
    const float p       = static_cast<float>(i + 1) / static_cast<float>(SHADOWMAP_CASCADE_COUNT);
    const float log     = min_z * SDL_powf(ratio, p);
    const float uniform = min_z + range * p;
    const float d       = cascade_split_lambda * (log - uniform) + uniform;
    cascade_splits[i]   = (d - near_clip) / clip_range;
  }

  float last_split_dist = 0.0;
  for (uint32_t cascade_idx = 0; cascade_idx < SHADOWMAP_CASCADE_COUNT; cascade_idx++)
  {
    //
    // Frustum edges overview
    //
    //         4 --- 5     Y
    //       /     / |     /\  Z
    //     0 --- 1   |     | /
    //     |     |   6     .--> X
    //     |     | /
    //     3 --- 2
    //
    Vec3 frustum_corners[] = {
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, 1.0f},
    };

    //
    // LoD change should follow main game camera and not the light projection.
    // Because of that frustums have to "come out" from viewer camera.
    //
    Mat4x4 inv_cam = (camera_projection * camera_view).invert();

    for (Vec3& in : frustum_corners)
    {
      Vec4 inv_corner = inv_cam * Vec4(in, 1.0f);
      in              = inv_corner.as_vec3().scale(1.0f / inv_corner.w);
    }

    const float split_dist = cascade_splits[cascade_idx];
    for (uint32_t i = 0; i < 4; i++)
    {
      const Vec3 dist        = frustum_corners[i + 4] - frustum_corners[i];
      frustum_corners[i + 4] = frustum_corners[i] + dist.scale(split_dist);
      frustum_corners[i] += dist.scale(last_split_dist);
    }

    Vec3 frustum_center;
    for (Vec3& frustum_corner : frustum_corners)
    {
      frustum_center += frustum_corner;
    }
    frustum_center = frustum_center.scale(1.0f / 8.0f);

    float radius = 0.0f;
    for (const Vec3& frustum_corner : frustum_corners)
    {
      const float distance = (frustum_corner - frustum_center).len();
      radius               = std::max(radius, distance);
    }

    Vec3 max_extents = Vec3(SDL_ceilf(radius * 16.0f) / 16.0f);
    Vec3 min_extents = max_extents.invert_signs();
    Vec3 light_dir   = light_source_position.invert_signs().normalize();

    const Mat4x4 light_view_mat =
        Mat4x4::LookAt(frustum_center - light_dir.scale(-min_extents.z), frustum_center, Vec3(0.0f, -1.0f, 0.0f));

    // todo: I don't know why the near clipping plane has to be a huge negative number! If used with 0 as in tutorials,
    //       the depth is not calculated properly.. I guess for now it'll have to be this way.

    Mat4x4 light_ortho_mat;
    light_ortho_mat.ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, -50.0f,
                          max_extents.z - min_extents.z);

    cascade_view_proj_mat[cascade_idx] = light_ortho_mat * light_view_mat;
    float cascade_split_depth          = near_clip + split_dist * clip_range;
    cascade_split_depths[cascade_idx]  = cascade_split_depth;
    last_split_dist                    = cascade_splits[cascade_idx];
  }
}
