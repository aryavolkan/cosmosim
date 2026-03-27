#include "integrator.h"

void integrator_init_accelerations(
    Body *bodies, int n, double G, double softening, double theta, OctreeNode *pool)
{
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, n);
    octree_compute_forces(pool, 0, bodies, n, G, softening, theta);
}

void integrator_step(
    Body *bodies, int n, double dt, double G, double softening, double theta, OctreeNode *pool)
{
    // Kick (half step) + Drift (full step)
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0)
            continue;
        bodies[i].vx += 0.5 * bodies[i].ax * dt;
        bodies[i].vy += 0.5 * bodies[i].ay * dt;
        bodies[i].vz += 0.5 * bodies[i].az * dt;
        bodies[i].x += bodies[i].vx * dt;
        bodies[i].y += bodies[i].vy * dt;
        bodies[i].z += bodies[i].vz * dt;
    }

    // Recompute accelerations
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, n);
    octree_compute_forces(pool, 0, bodies, n, G, softening, theta);

    // Kick (half step)
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0)
            continue;
        bodies[i].vx += 0.5 * bodies[i].ax * dt;
        bodies[i].vy += 0.5 * bodies[i].ay * dt;
        bodies[i].vz += 0.5 * bodies[i].az * dt;
    }
}
