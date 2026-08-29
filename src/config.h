#ifndef MACOS_TRACKPOINT_SCROLL_CONFIG_H
#define MACOS_TRACKPOINT_SCROLL_CONFIG_H

#include <stdbool.h>

struct macos_trackpoint_config {
    bool natural_scroll;
    double scroll_scale;
    bool suppress_middle_click;
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
