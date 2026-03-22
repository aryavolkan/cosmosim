#ifndef INITIAL_CONDITIONS_H
#define INITIAL_CONDITIONS_H

#include "body.h"

void generate_spiral_galaxy(Body *bodies, int n, double cx, double cy,
                            double galaxy_mass, double disk_radius,
                            double vx_bulk, double vy_bulk);

void generate_merger(Body *bodies, int n, double separation, double approach_vel);

#endif
