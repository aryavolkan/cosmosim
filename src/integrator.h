#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include "body.h"
#include "quadtree.h"

void integrator_step(Body *bodies, int n, double dt, double G,
                     double softening, double theta, QuadTreeNode *pool);

void integrator_init_accelerations(Body *bodies, int n, double G,
                                   double softening, double theta, QuadTreeNode *pool);

#endif
