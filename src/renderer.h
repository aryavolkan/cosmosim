#ifndef RENDERER_H
#define RENDERER_H

#include "body.h"

typedef struct {
    float azimuth;    // horizontal angle in radians
    float elevation;  // vertical angle in radians
    float distance;   // distance from target
    float target_x, target_y, target_z;  // look-at target
} Camera;

int renderer_init(void);
void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height);
void renderer_cleanup(void);

#endif
