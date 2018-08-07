#pragma once

#include "game.hh"

namespace render {

int skybox_job(ThreadJobData tjd);
int robot_job(ThreadJobData tjd);
int helmet_job(ThreadJobData tjd);
int point_light_boxes(ThreadJobData tjd);
int matrioshka_box(ThreadJobData tjd);
int vr_scene(ThreadJobData tjd);
int simple_rigged(ThreadJobData tjd);
int monster_rigged(ThreadJobData tjd);
int radar(ThreadJobData tjd);
int robot_gui_lines(ThreadJobData tjd);
int robot_gui_speed_meter_text(ThreadJobData tjd);
int robot_gui_speed_meter_triangle(ThreadJobData tjd);
int height_ruler_text(ThreadJobData tjd);
int tilt_ruler_text(ThreadJobData tjd);
int compass_text(ThreadJobData tjd);
int radar_dots(ThreadJobData tjd);
int weapon_selectors_left(ThreadJobData tjd);
int weapon_selectors_right(ThreadJobData tjd);
int hello_world_text(ThreadJobData tjd);
int imgui(ThreadJobData tjd);
int water(ThreadJobData tjd);

} // namespace render
