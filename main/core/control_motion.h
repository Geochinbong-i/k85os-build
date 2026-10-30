#pragma once
#include "imu_math.h"
bool k85_control_vector(K85Vec &out);
void k85_control_motion_reset();
bool k85_control_calibrate();
void k85_display_rotation(int absolute_rotation);
void k85_controls_menu();
void k85_display_menu();
