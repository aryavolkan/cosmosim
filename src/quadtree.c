#include "quadtree.h"
#include <math.h>
#include <float.h>

#define MAX_DEPTH 64

static int alloc_node(QuadTreeNode *pool, int *pool_size,
                      double center_x, double center_y, double half_size)
{
    int idx = (*pool_size)++;
    QuadTreeNode *n = &pool[idx];
    n->cx = 0.0;
    n->cy = 0.0;
    n->total_mass = 0.0;
    n->center_x = center_x;
    n->center_y = center_y;
    n->half_size = half_size;
    n->body_index = -1;
    n->children[0] = n->children[1] = n->children[2] = n->children[3] = -1;
    return idx;
}

static int quadrant(const QuadTreeNode *node, double x, double y)
{
    int q = 0;
    if (x >= node->center_x) q |= 1;  // east
    if (y < node->center_y) q |= 2;   // south
    return q; // 0=NW, 1=NE, 2=SW, 3=SE
}

static double child_offset(int q, int axis)
{
    // axis 0 = x, axis 1 = y
    if (axis == 0) return (q & 1) ? 0.5 : -0.5;
    return (q & 2) ? -0.5 : 0.5;
}

static void insert(QuadTreeNode *pool, int *pool_size, int node_idx,
                   const Body *bodies, int body_idx, int depth)
{
    QuadTreeNode *node = &pool[node_idx];
    double bx = bodies[body_idx].x;
    double by = bodies[body_idx].y;
    double bm = bodies[body_idx].mass;

    if (node->total_mass == 0.0 && node->body_index == -1) {
        // Empty node — place body here
        node->body_index = body_idx;
        node->cx = bx;
        node->cy = by;
        node->total_mass = bm;
        return;
    }

    if (depth >= MAX_DEPTH) {
        // Max depth — just accumulate mass
        double new_mass = node->total_mass + bm;
        node->cx = (node->cx * node->total_mass + bx * bm) / new_mass;
        node->cy = (node->cy * node->total_mass + by * bm) / new_mass;
        node->total_mass = new_mass;
        return;
    }

    // If leaf, subdivide: push existing body into child
    if (node->body_index >= 0) {
        int old_idx = node->body_index;
        node->body_index = -1;

        int q = quadrant(node, bodies[old_idx].x, bodies[old_idx].y);
        double hs = node->half_size * 0.5;
        double cx = node->center_x + child_offset(q, 0) * node->half_size;
        double cy = node->center_y + child_offset(q, 1) * node->half_size;
        node->children[q] = alloc_node(pool, pool_size, cx, cy, hs);
        insert(pool, pool_size, node->children[q], bodies, old_idx, depth + 1);
    }

    // Update center of mass
    double new_mass = node->total_mass + bm;
    node->cx = (node->cx * node->total_mass + bx * bm) / new_mass;
    node->cy = (node->cy * node->total_mass + by * bm) / new_mass;
    node->total_mass = new_mass;

    // Insert new body into appropriate child
    int q = quadrant(node, bx, by);
    if (node->children[q] < 0) {
        double hs = node->half_size * 0.5;
        double cx = node->center_x + child_offset(q, 0) * node->half_size;
        double cy = node->center_y + child_offset(q, 1) * node->half_size;
        node->children[q] = alloc_node(pool, pool_size, cx, cy, hs);
    }
    insert(pool, pool_size, node->children[q], bodies, body_idx, depth + 1);
}

void quadtree_build(QuadTreeNode *pool, int *pool_size, const Body *bodies, int n)
{
    *pool_size = 0;

    // Compute bounding box
    double min_x = DBL_MAX, max_x = -DBL_MAX;
    double min_y = DBL_MAX, max_y = -DBL_MAX;
    for (int i = 0; i < n; i++) {
        if (bodies[i].x < min_x) min_x = bodies[i].x;
        if (bodies[i].x > max_x) max_x = bodies[i].x;
        if (bodies[i].y < min_y) min_y = bodies[i].y;
        if (bodies[i].y > max_y) max_y = bodies[i].y;
    }

    double cx = (min_x + max_x) * 0.5;
    double cy = (min_y + max_y) * 0.5;
    double size_x = max_x - min_x;
    double size_y = max_y - min_y;
    double half_size = (size_x > size_y ? size_x : size_y) * 0.5 * 1.01;
    if (half_size < 1e-10) half_size = 1.0;

    int root = alloc_node(pool, pool_size, cx, cy, half_size);
    (void)root; // always 0

    for (int i = 0; i < n; i++) {
        insert(pool, pool_size, 0, bodies, i, 0);
    }
}

static void compute_force_on_body(const QuadTreeNode *pool, int node_idx,
                                  Body *body, int body_idx,
                                  double G, double softening_sq, double theta)
{
    const QuadTreeNode *node = &pool[node_idx];

    if (node->total_mass == 0.0) return;

    double dx = node->cx - body->x;
    double dy = node->cy - body->y;
    double dist_sq = dx * dx + dy * dy;

    // Leaf node
    if (node->body_index >= 0) {
        if (node->body_index == body_idx) return;
        double r_sq = dist_sq + softening_sq;
        double inv_r3 = 1.0 / (r_sq * sqrt(r_sq));
        body->ax += G * node->total_mass * dx * inv_r3;
        body->ay += G * node->total_mass * dy * inv_r3;
        return;
    }

    // Internal node — check Barnes-Hut criterion
    double s = 2.0 * node->half_size;
    if (dist_sq > 0.0 && (s * s) / dist_sq < (theta * theta)) {
        double r_sq = dist_sq + softening_sq;
        double inv_r3 = 1.0 / (r_sq * sqrt(r_sq));
        body->ax += G * node->total_mass * dx * inv_r3;
        body->ay += G * node->total_mass * dy * inv_r3;
        return;
    }

    // Recurse into children
    for (int c = 0; c < 4; c++) {
        if (node->children[c] >= 0) {
            compute_force_on_body(pool, node->children[c], body, body_idx,
                                  G, softening_sq, theta);
        }
    }
}

void quadtree_compute_forces(const QuadTreeNode *pool, int root, Body *bodies, int n,
                             double G, double softening, double theta)
{
    double softening_sq = softening * softening;

    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; i++) {
        bodies[i].ax = 0.0;
        bodies[i].ay = 0.0;
        compute_force_on_body(pool, root, &bodies[i], i, G, softening_sq, theta);
    }
}
