#ifndef OCTREE_H
#define OCTREE_H

#include "body.h"

typedef struct {
    double cx, cy, cz; // center of mass
    double total_mass;
    double center_x, center_y, center_z; // geometric center of cell
    double half_size;
    int body_index;  // -1 if internal or empty
    int children[8]; // 0=NW-top, 1=NE-top, 2=SW-top, 3=SE-top,
                     // 4=NW-bot, 5=NE-bot, 6=SW-bot, 7=SE-bot; -1 if absent
} OctreeNode;

void octree_build(OctreeNode *pool, int *pool_size, const Body *bodies, int n);
void octree_compute_forces(const OctreeNode *pool,
                           int root,
                           Body *bodies,
                           int n,
                           double G,
                           double softening,
                           double theta);

#endif
