#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include "body.h"
#include "octree.h"

void integrator_step(Body *bodies,
                     int n,
                     double dt,
                     double G,
                     double softening,
                     double theta,
                     OctreeNode *pool,
                     int sph_enabled);

void integrator_init_accelerations(
    Body *bodies, int n, double G, double softening, double theta, OctreeNode *pool);

#endif
