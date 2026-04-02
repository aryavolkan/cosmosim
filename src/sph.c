#include "sph.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Cubic spline kernel (M4) ──────────────────────────────────────────── */

static double kernel_W(double r, double h)
{
    if (h < 1e-10)
        return 0.0;
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
    if (h < 1e-10)
        return 0.0;
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
        int nn = octree_find_neighbors(tree,
                                       0,
                                       bodies,
                                       n,
                                       bodies[i].x,
                                       bodies[i].y,
                                       bodies[i].z,
                                       h,
                                       neighbor_buf,
                                       SPH_MAX_NEIGHBORS);

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

        if (rho < 1e-15 || isnan(rho))
            rho = 1e-15;
        bodies[i].density = rho;

        /* Equation of state: P = (γ-1) * ρ * u */
        double u = bodies[i].internal_energy;
        if (u < 1e-15)
            u = 1e-15;
        bodies[i].pressure = (SPH_GAMMA - 1.0) * rho * u;
    }
}

/* ── SPH forces: pressure gradient + Monaghan artificial viscosity ──── */

void sph_compute_forces(Body *bodies, int n, const OctreeNode *tree)
{
    int neighbor_buf[SPH_MAX_NEIGHBORS];

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) private(neighbor_buf)
#endif
    for (int i = 0; i < n; i++) {
        if (bodies[i].type != BODY_GAS || bodies[i].mass <= 0.0)
            continue;

        double h_i = bodies[i].smoothing_h;
        double rho_i = bodies[i].density;
        double P_i = bodies[i].pressure;
        double u_i = bodies[i].internal_energy;
        if (u_i < 1e-15)
            u_i = 1e-15;
        double c_i = sqrt(SPH_GAMMA * P_i / (rho_i + 1e-15));

        int nn = octree_find_neighbors(tree,
                                       0,
                                       bodies,
                                       n,
                                       bodies[i].x,
                                       bodies[i].y,
                                       bodies[i].z,
                                       h_i,
                                       neighbor_buf,
                                       SPH_MAX_NEIGHBORS);

        double ax_sph = 0.0, ay_sph = 0.0, az_sph = 0.0;

        for (int k = 0; k < nn; k++) {
            int j = neighbor_buf[k];
            if (j == i || bodies[j].mass <= 0.0)
                continue;
            /* Only gas-gas interactions for SPH forces */
            if (bodies[j].type != BODY_GAS)
                continue;

            double dx = bodies[i].x - bodies[j].x;
            double dy = bodies[i].y - bodies[j].y;
            double dz = bodies[i].z - bodies[j].z;
            double r = sqrt(dx * dx + dy * dy + dz * dz);
            if (r < 1e-15)
                continue;

            double h_j = bodies[j].smoothing_h;
            double h_avg = 0.5 * (h_i + h_j);
            double rho_j = bodies[j].density;
            double P_j = bodies[j].pressure;
            /* Monaghan artificial viscosity */
            double dvx = bodies[i].vx - bodies[j].vx;
            double dvy = bodies[i].vy - bodies[j].vy;
            double dvz = bodies[i].vz - bodies[j].vz;
            double vr = dvx * dx + dvy * dy + dvz * dz;

            double Pi_ij = 0.0;
            if (vr < 0.0) { /* particles approaching */
                double mu = h_avg * vr / (r * r + 0.01 * h_avg * h_avg);
                double c_j = sqrt(SPH_GAMMA * P_j / (rho_j + 1e-15));
                double c_avg = 0.5 * (c_i + c_j);
                double rho_avg = 0.5 * (rho_i + rho_j);
                double alpha_visc = 1.0;
                double beta_visc = 2.0;
                Pi_ij = (-alpha_visc * c_avg * mu + beta_visc * mu * mu) / (rho_avg + 1e-15);
            }

            /* Kernel gradient (scalar part — direction is r_hat) */
            double dWdr = kernel_dWdr(r, h_avg);
            double dWdx = dWdr * dx / r;
            double dWdy = dWdr * dy / r;
            double dWdz = dWdr * dz / r;

            /* Pressure gradient + viscosity */
            double factor = bodies[j].mass *
                            (P_i / (rho_i * rho_i + 1e-15) + P_j / (rho_j * rho_j + 1e-15) + Pi_ij);

            ax_sph -= factor * dWdx;
            ay_sph -= factor * dWdy;
            az_sph -= factor * dWdz;
        }

        /* NaN guard + scale down to prevent blowout */
        if (isnan(ax_sph))
            ax_sph = 0.0;
        if (isnan(ay_sph))
            ay_sph = 0.0;
        if (isnan(az_sph))
            az_sph = 0.0;
        double sph_scale = 0.1;
        bodies[i].ax += ax_sph * sph_scale;
        bodies[i].ay += ay_sph * sph_scale;
        bodies[i].az += az_sph * sph_scale;
    }
}

/* ── Radiative cooling ─────────────────────────────────────────────────── */

void sph_apply_cooling(Body *bodies, int n, double dt)
{
    /* Bremsstrahlung-approximation: du/dt = -Λ₀ * ρ * sqrt(u)
       Integrated implicitly to prevent negative energy. */
    double lambda0 = 0.05; /* tuned so disk forms in ~100 dynamical times */

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < n; i++) {
        if (bodies[i].type != BODY_GAS || bodies[i].mass <= 0.0)
            continue;

        double u = bodies[i].internal_energy;
        double rho = bodies[i].density;
        if (u < 1e-15 || rho < 1e-15)
            continue;

        /* Implicit Euler: u_new = u / (1 + dt * Λ₀ * ρ * 0.5 / sqrt(u)) */
        double denom = 1.0 + dt * lambda0 * rho * 0.5 / sqrt(u);
        double u_new = u / denom;

        /* Temperature floor: 1% of initial thermal energy */
        double u_min = 0.01;
        if (u_new < u_min)
            u_new = u_min;

        bodies[i].internal_energy = u_new;
        /* Update pressure with new energy */
        bodies[i].pressure = (SPH_GAMMA - 1.0) * rho * u_new;
    }
}
