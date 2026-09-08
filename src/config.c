#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *
trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    return s;
}

static int
parse_bool(const char *s, bool *out)
{
    if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 ||
        strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) {
        *out = true;
        return 0;
    }
    if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 ||
        strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int
parse_nonnegative_double(const char *s, double *out)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(s, &end);
    if (errno != 0 || end == s || *trim(end) != '\0' ||
        !isfinite(value) || value < 0.0)
        return -1;
    *out = value;
    return 0;
}

static int
parse_positive_double(const char *s, double *out)
{
    if (parse_nonnegative_double(s, out) != 0 || *out <= 0.0)
        return -1;
    return 0;
}

void
macos_trackpoint_config_defaults(struct macos_trackpoint_config *cfg)
{
    cfg->natural_scroll = false;
    cfg->scroll_scale = 8.0;
    cfg->suppress_middle_click = true;
    cfg->rebound_filter = false;
    cfg->pointer_speed = 1.0;
    cfg->pointer_acceleration = 0.0;
    cfg->pointer_acceleration_velocity = 0.10;
}

const char *
macos_trackpoint_default_config_path(char *buffer, unsigned long size)
{
    const char *home = getenv("HOME");
    int written;

    if (!home || !*home || !buffer || size == 0)
        return NULL;

    written = snprintf(buffer, (size_t)size,
                       "%s/.config/macOS-trackpoint-scroll.conf", home);
    if (written < 0 || (unsigned long)written >= size)
        return NULL;
    return buffer;
}

int
macos_trackpoint_config_load(struct macos_trackpoint_config *cfg,
                             const char *path,
                             bool verbose)
{
    FILE *file;
    char line[1024];
    unsigned long line_no = 0;

    if (!path || !*path)
        return 0;

    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT)
            return 0;
        fprintf(stderr, "trackpoint: cannot open config %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char *key;
        char *value;
        char *equals;
        char *comment;

        line_no++;
        key = trim(line);
        if (*key == '\0' || *key == '#' || *key == ';')
            continue;

        comment = strpbrk(key, "#;");
        if (comment)
            *comment = '\0';

        equals = strchr(key, '=');
        if (!equals) {
            fprintf(stderr, "trackpoint: %s:%lu: expected key=value\n",
                    path, line_no);
            fclose(file);
            return -1;
        }
        *equals = '\0';
        value = trim(equals + 1);
        key = trim(key);

        if (strcmp(key, "natural_scroll") == 0) {
            if (parse_bool(value, &cfg->natural_scroll) != 0)
                goto invalid_value;
        } else if (strcmp(key, "scroll_scale") == 0) {
            if (parse_positive_double(value, &cfg->scroll_scale) != 0)
                goto invalid_value;
        } else if (strcmp(key, "suppress_middle_click") == 0) {
            if (parse_bool(value, &cfg->suppress_middle_click) != 0)
                goto invalid_value;
        } else if (strcmp(key, "rebound_filter") == 0) {
            if (parse_bool(value, &cfg->rebound_filter) != 0)
                goto invalid_value;
        } else if (strcmp(key, "pointer_speed") == 0) {
            if (parse_positive_double(value, &cfg->pointer_speed) != 0)
                goto invalid_value;
        } else if (strcmp(key, "pointer_acceleration") == 0) {
            if (parse_nonnegative_double(value, &cfg->pointer_acceleration) != 0)
                goto invalid_value;
        } else if (strcmp(key, "pointer_acceleration_velocity") == 0) {
            if (parse_positive_double(value,
                                      &cfg->pointer_acceleration_velocity) != 0)
                goto invalid_value;
        } else {
            fprintf(stderr, "trackpoint: %s:%lu: unknown key '%s'\n",
                    path, line_no, key);
            fclose(file);
            return -1;
        }
        continue;

invalid_value:
        fprintf(stderr, "trackpoint: %s:%lu: invalid value for %s: '%s'\n",
                path, line_no, key, value);
        fclose(file);
        return -1;
    }

    if (ferror(file)) {
        fprintf(stderr, "trackpoint: error reading config %s\n", path);
        fclose(file);
        return -1;
    }

    fclose(file);
    if (verbose)
        fprintf(stderr, "trackpoint: loaded config %s\n", path);
    return 0;
}
