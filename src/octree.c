#include "octree.h"
#include <math.h>
#include <float.h>

#define MAX_DEPTH 64

static int alloc_node(OctreeNode *pool,
                      int *pool_size,
                      double center_x,
                      double center_y,
                      double center_z,
                      double half_size)
{
    int idx = (*pool_size)++;
    OctreeNode *n = &pool[idx];
    n->cx = 0.0;
    n->cy = 0.0;
    n->cz = 0.0;
    n->total_mass = 0.0;
    n->center_x = center_x;
    n->center_y = center_y;
    n->center_z = center_z;
    n->half_size = half_size;
    n->body_index = -1;
    for (int i = 0; i < 8; i++)
        n->children[i] = -1;
    return idx;
}

static int octant(const OctreeNode *node, double x, double y, double z)
{
    int q = 0;
    if (x >= node->center_x)
        q |= 1; // east
    if (y < node->center_y)
        q |= 2; // south
    if (z < node->center_z)
        q |= 4; // bottom
    return q;
}

static double child_offset(int q, int axis)
{
    // axis 0=x, 1=y, 2=z
    if (axis == 0)
        return (q & 1) ? 0.5 : -0.5;
    if (axis == 1)
        return (q & 2) ? -0.5 : 0.5;
    return (q & 4) ? -0.5 : 0.5;
}

static void
insert(OctreeNode *pool, int *pool_size, int node_idx, const Body *bodies, int body_idx, int depth)
{
    OctreeNode *node = &pool[node_idx];
    double bx = bodies[body_idx].x;
    double by = bodies[body_idx].y;
    double bz = bodies[body_idx].z;
    double bm = bodies[body_idx].mass;

    if (node->total_mass == 0.0 && node->body_index == -1) {
        // Empty node — place body here
        node->body_index = body_idx;
        node->cx = bx;
        node->cy = by;
        node->cz = bz;
        node->total_mass = bm;
        return;
    }

    if (depth >= MAX_DEPTH) {
        // Max depth — just accumulate mass
        double new_mass = node->total_mass + bm;
        node->cx = (node->cx * node->total_mass + bx * bm) / new_mass;
        node->cy = (node->cy * node->total_mass + by * bm) / new_mass;
        node->cz = (node->cz * node->total_mass + bz * bm) / new_mass;
        node->total_mass = new_mass;
        return;
    }

    // If leaf, subdivide: push existing body into child
    if (node->body_index >= 0) {
        int old_idx = node->body_index;
        node->body_index = -1;

        int q = octant(node, bodies[old_idx].x, bodies[old_idx].y, bodies[old_idx].z);
        double hs = node->half_size * 0.5;
        double cx = node->center_x + child_offset(q, 0) * node->half_size;
        double cy = node->center_y + child_offset(q, 1) * node->half_size;
        double cz = node->center_z + child_offset(q, 2) * node->half_size;
        node->children[q] = alloc_node(pool, pool_size, cx, cy, cz, hs);
        insert(pool, pool_size, node->children[q], bodies, old_idx, depth + 1);
    }

    // Update center of mass
    double new_mass = node->total_mass + bm;
    node->cx = (node->cx * node->total_mass + bx * bm) / new_mass;
    node->cy = (node->cy * node->total_mass + by * bm) / new_mass;
    node->cz = (node->cz * node->total_mass + bz * bm) / new_mass;
    node->total_mass = new_mass;

    // Insert new body into appropriate child
    int q = octant(node, bx, by, bz);
    if (node->children[q] < 0) {
        double hs = node->half_size * 0.5;
        double cx = node->center_x + child_offset(q, 0) * node->half_size;
        double cy = node->center_y + child_offset(q, 1) * node->half_size;
        double cz = node->center_z + child_offset(q, 2) * node->half_size;
        node->children[q] = alloc_node(pool, pool_size, cx, cy, cz, hs);
    }
    insert(pool, pool_size, node->children[q], bodies, body_idx, depth + 1);
}

void octree_build(OctreeNode *pool, int *pool_size, const Body *bodies, int n)
{
    *pool_size = 0;

    // Compute bounding box (skip dead bodies)
    double min_x = DBL_MAX, max_x = -DBL_MAX;
    double min_y = DBL_MAX, max_y = -DBL_MAX;
    double min_z = DBL_MAX, max_z = -DBL_MAX;
    int alive_count = 0;
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0)
            continue;
        alive_count++;
        if (bodies[i].x < min_x)
            min_x = bodies[i].x;
        if (bodies[i].x > max_x)
            max_x = bodies[i].x;
        if (bodies[i].y < min_y)
            min_y = bodies[i].y;
        if (bodies[i].y > max_y)
            max_y = bodies[i].y;
        if (bodies[i].z < min_z)
            min_z = bodies[i].z;
        if (bodies[i].z > max_z)
            max_z = bodies[i].z;
    }

    if (alive_count == 0) {
        alloc_node(pool, pool_size, 0, 0, 0, 1.0);
        return;
    }

    double cx = (min_x + max_x) * 0.5;
    double cy = (min_y + max_y) * 0.5;
    double cz = (min_z + max_z) * 0.5;
    double size_x = max_x - min_x;
    double size_y = max_y - min_y;
    double size_z = max_z - min_z;
    double half_size = size_x;
    if (size_y > half_size)
        half_size = size_y;
    if (size_z > half_size)
        half_size = size_z;
    half_size = half_size * 0.5 * 1.01;
    if (half_size < 1e-10)
        half_size = 1.0;

    int root = alloc_node(pool, pool_size, cx, cy, cz, half_size);
    (void)root; // always 0

    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0)
            continue;
        insert(pool, pool_size, 0, bodies, i, 0);
    }
}

static void compute_force_on_body(const OctreeNode *pool,
                                  int node_idx,
                                  Body *body,
                                  int body_idx,
                                  double G,
                                  double softening_sq,
                                  double theta)
{
    const OctreeNode *node = &pool[node_idx];

    if (node->total_mass == 0.0)
        return;

    double dx = node->cx - body->x;
    double dy = node->cy - body->y;
    double dz = node->cz - body->z;
    double dist_sq = dx * dx + dy * dy + dz * dz;

    // Leaf node
    if (node->body_index >= 0) {
        if (node->body_index == body_idx)
            return;
        double r_sq = dist_sq + softening_sq;
        double inv_r3 = 1.0 / (r_sq * sqrt(r_sq));
        body->ax += G * node->total_mass * dx * inv_r3;
        body->ay += G * node->total_mass * dy * inv_r3;
        body->az += G * node->total_mass * dz * inv_r3;
        return;
    }

    // Internal node — check Barnes-Hut criterion
    double s = 2.0 * node->half_size;
    if (dist_sq > 0.0 && (s * s) / dist_sq < (theta * theta)) {
        double r_sq = dist_sq + softening_sq;
        double inv_r3 = 1.0 / (r_sq * sqrt(r_sq));
        body->ax += G * node->total_mass * dx * inv_r3;
        body->ay += G * node->total_mass * dy * inv_r3;
        body->az += G * node->total_mass * dz * inv_r3;
        return;
    }

    // Recurse into children
    for (int c = 0; c < 8; c++) {
        if (node->children[c] >= 0) {
            compute_force_on_body(pool, node->children[c], body, body_idx, G, softening_sq, theta);
        }
    }
}

void octree_compute_forces(
    const OctreeNode *pool, int root, Body *bodies, int n, double G, double softening, double theta)
{
    double softening_sq = softening * softening;

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; i++) {
        bodies[i].ax = 0.0;
        bodies[i].ay = 0.0;
        bodies[i].az = 0.0;
        if (bodies[i].mass <= 0.0)
            continue;
        compute_force_on_body(pool, root, &bodies[i], i, G, softening_sq, theta);
    }
}
