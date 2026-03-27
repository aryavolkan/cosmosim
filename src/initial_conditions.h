#ifndef INITIAL_CONDITIONS_H
#define INITIAL_CONDITIONS_H

#include "body.h"

void generate_spiral_galaxy(Body *bodies,
                            int n,
                            double cx,
                            double cy,
                            double galaxy_mass,
                            double disk_radius,
                            double vx_bulk,
                            double vy_bulk);

void generate_merger(Body *bodies, int n, double separation, double approach_vel);

// Quasar variants: generates SMBH + gas-seeded inner disk
void generate_quasar_galaxy(Body *bodies,
                            int n,
                            double cx,
                            double cy,
                            double galaxy_mass,
                            double disk_radius,
                            double vx_bulk,
                            double vy_bulk,
                            double smbh_mass_frac);

void generate_quasar_merger(
    Body *bodies, int n, double separation, double approach_vel, double smbh_mass_frac);

// Spawn interstellar dust particles (tidal streams + outer disk halos) after galaxy generation.
// bodies[start_idx .. start_idx+n_dust-1] must be pre-allocated (calloc'd to zero).
void generate_merger_dust(Body *bodies, int start_idx, int n_dust, double separation);

#endif
