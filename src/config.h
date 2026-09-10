#ifndef MACOS_TRACKPOINT_SCROLL_CONFIG_H
#define MACOS_TRACKPOINT_SCROLL_CONFIG_H

#include <stdbool.h>

#include "trackpoint_scroll/profiles.h"

struct macos_trackpoint_config {
    bool natural_scroll;
    double scroll_scale;

    enum tpsc_profile_kind profile;
    bool clamp_negative_output;
    double first_step_distance;
    double first_step_axis_merge_ms;
    int first_step_max_reports;
    double idle_reset_ms;

    double affine_k;
    double affine_b;

    double quadratic_a;
    double quadratic_h;
    double quadratic_k;

    double hyperbolic_a;
    double hyperbolic_u;
    double hyperbolic_k;

    bool suppress_middle_click;
    bool rebound_filter;
    double pointer_speed;
    double pointer_acceleration;
    double pointer_acceleration_velocity;
};

void macos_trackpoint_config_defaults(struct macos_trackpoint_config *cfg);
int macos_trackpoint_config_load(struct macos_trackpoint_config *cfg,
                                 const char *path,
                                 bool verbose);
const char *macos_trackpoint_default_config_path(char *buffer, unsigned long size);

#endif
