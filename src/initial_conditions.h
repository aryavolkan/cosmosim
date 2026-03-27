#ifndef INITIAL_CONDITIONS_H
#define INITIAL_CONDITIONS_H

#include "body.h"

void generate_spiral_galaxy(Body *bodies, int n, double cx, double cy,
                            double galaxy_mass, double disk_radius,
                            double vx_bulk, double vy_bulk);

void generate_merger(Body *bodies, int n, double separation, double approach_vel);

// Quasar variants: generates SMBH + gas-seeded inner disk
void generate_quasar_galaxy(Body *bodies, int n, double cx, double cy,
                            double galaxy_mass, double disk_radius,
                            double vx_bulk, double vy_bulk,
                            double smbh_mass_frac);

void generate_quasar_merger(Body *bodies, int n, double separation,
                            double approach_vel, double smbh_mass_frac);

#endif
