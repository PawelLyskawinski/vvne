#pragma once

#include "math.hh"

void recalculate_cascade_view_proj_matrices(Mat4x4* cascade_view_proj_mat, float* cascade_split_depths,
                                            Mat4x4 camera_projection, Mat4x4 camera_view, Vec3 light_source_position);
