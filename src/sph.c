#include "sph.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Cubic spline kernel (M4) ──────────────────────────────────────────── */

static double kernel_W(double r, double h)
{
    double q = r / h;
    if (q > 1.0)
        return 0.0;
    double norm = 8.0 / (M_PI * h * h * h);
    if (q <= 0.5) {
        return norm * (1.0 - 6.0 * q * q + 6.0 * q * q * q);
    } else {
        double t = 1.0 - q;
        return norm * 2.0 * t * t * t;
    }
}

static double kernel_dWdr(double r, double h)
{
    double q = r / h;
    if (q > 1.0 || r < 1e-15)
        return 0.0;
    double norm = 8.0 / (M_PI * h * h * h);
    if (q <= 0.5) {
        return norm * (-12.0 * q + 18.0 * q * q) / h;
    } else {
        double t = 1.0 - q;
        return norm * (-6.0 * t * t) / h;
    }
}

/* ── Density estimation ────────────────────────────────────────────────── */

void sph_compute_density(Body *bodies, int n, const OctreeNode *tree)
{
    int neighbor_buf[SPH_MAX_NEIGHBORS];

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) private(neighbor_buf)
#endif
    for (int i = 0; i < n; i++) {
        if (bodies[i].type != BODY_GAS || bodies[i].mass <= 0.0)
            continue;

        double h = bodies[i].smoothing_h;
        int nn = octree_find_neighbors(tree, 0, bodies, n,
                                       bodies[i].x, bodies[i].y, bodies[i].z,
                                       h, neighbor_buf, SPH_MAX_NEIGHBORS);

        /* Adaptive smoothing: adjust h to get ~TARGET neighbors */
        if (nn < 20 && h < 50.0)
            bodies[i].smoothing_h = h * 1.2;
        else if (nn > 48 && h > 0.1)
            bodies[i].smoothing_h = h * 0.8;

        /* Compute density: ρ = Σ m_j W(r_ij, h) */
        double rho = 0.0;
        for (int k = 0; k < nn; k++) {
            int j = neighbor_buf[k];
            if (bodies[j].mass <= 0.0)
                continue;
            double dx = bodies[i].x - bodies[j].x;
            double dy = bodies[i].y - bodies[j].y;
            double dz = bodies[i].z - bodies[j].z;
            double r = sqrt(dx * dx + dy * dy + dz * dz);
            rho += bodies[j].mass * kernel_W(r, h);
        }

        if (rho < 1e-15)
            rho = 1e-15;
        bodies[i].density = rho;

        /* Equation of state: P = (γ-1) * ρ * u */
        double u = bodies[i].internal_energy;
        if (u < 1e-15)
            u = 1e-15;
        bodies[i].pressure = (SPH_GAMMA - 1.0) * rho * u;
    }
}

void sph_compute_forces(Body *bodies, int n, const OctreeNode *tree)
{
    (void)bodies; (void)n; (void)tree;
    /* Implemented in Task 4 */
}

void sph_apply_cooling(Body *bodies, int n, double dt)
{
    (void)bodies; (void)n; (void)dt;
    /* Implemented in Task 5 */
}
