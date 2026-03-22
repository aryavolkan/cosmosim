#ifndef QUADTREE_H
#define QUADTREE_H

#include "body.h"

typedef struct {
    double cx, cy;              // center of mass
    double total_mass;
    double center_x, center_y;  // geometric center of cell
    double half_size;
    int body_index;             // -1 if internal or empty
    int children[4];            // NW=0, NE=1, SW=2, SE=3; -1 if absent
} QuadTreeNode;

void quadtree_build(QuadTreeNode *pool, int *pool_size, const Body *bodies, int n);
void quadtree_compute_forces(const QuadTreeNode *pool, int root, Body *bodies, int n,
                             double G, double softening, double theta);

#endif
