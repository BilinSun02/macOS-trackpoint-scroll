#ifndef MACOS_TRACKPOINT_SCROLL_POINTER_ACCEL_H
#define MACOS_TRACKPOINT_SCROLL_POINTER_ACCEL_H

#include <stdint.h>

struct tpsc_pointer_accel {
    double base_speed;
    double acceleration;
    double velocity_scale;

    uint64_t report_time_us;
    double report_dx;
    double report_dy;
    double gain;
    double remainder_x;
    double remainder_y;
    int have_report;
};

void tpsc_pointer_accel_init(struct tpsc_pointer_accel *state,
                             double base_speed,
                             double acceleration,
                             double velocity_scale);
void tpsc_pointer_accel_reset(struct tpsc_pointer_accel *state);
int64_t tpsc_pointer_accel_axis(struct tpsc_pointer_accel *state,
                                uint64_t time_us,
                                int axis,
                                int64_t delta);

#endif
