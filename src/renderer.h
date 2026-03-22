#ifndef RENDERER_H
#define RENDERER_H

#include "body.h"

typedef struct {
    float offset_x, offset_y;
    float zoom;
} Camera;

int renderer_init(void);
void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height);
void renderer_cleanup(void);

#endif
