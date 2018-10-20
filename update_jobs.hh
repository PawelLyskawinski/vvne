#pragma once

#include "game.hh"

namespace update {

void helmet_job(ThreadJobData tjd);
void robot_job(ThreadJobData tjd);
void monster_job(ThreadJobData tjd);
void rigged_simple_job(ThreadJobData tjd);
void moving_lights_job(ThreadJobData tjd);
void matrioshka_job(ThreadJobData tjd);
void orientation_axis_job(ThreadJobData tjd);

} // namespace update
