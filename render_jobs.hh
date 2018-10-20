#pragma once

#include "game.hh"

namespace render {

void skybox_job(ThreadJobData tjd);
void robot_job(ThreadJobData tjd);
void robot_depth_job(ThreadJobData tjd);
void helmet_job(ThreadJobData tjd);
void helmet_depth_job(ThreadJobData tjd);
void point_light_boxes(ThreadJobData tjd);
void matrioshka_box(ThreadJobData tjd);
void vr_scene(ThreadJobData tjd);
void vr_scene_depth(ThreadJobData tjd);
void simple_rigged(ThreadJobData tjd);
void monster_rigged(ThreadJobData tjd);
void radar(ThreadJobData tjd);
void robot_gui_lines(ThreadJobData tjd);
void robot_gui_speed_meter_text(ThreadJobData tjd);
void robot_gui_speed_meter_triangle(ThreadJobData tjd);
void height_ruler_text(ThreadJobData tjd);
void tilt_ruler_text(ThreadJobData tjd);
void compass_text(ThreadJobData tjd);
void radar_dots(ThreadJobData tjd);
void weapon_selectors_left(ThreadJobData tjd);
void weapon_selectors_right(ThreadJobData tjd);
void hello_world_text(ThreadJobData tjd);
void imgui(ThreadJobData tjd);
void water(ThreadJobData tjd);
void debug_shadowmap(ThreadJobData tjd);
void orientation_axis(ThreadJobData tjd);

} // namespace render
