#ifndef QUASAR_H
#define QUASAR_H

#include "body.h"

typedef struct {
    double accretion_radius;    // r_acc: bodies inside this radius are candidates
    double swallow_radius;      // r_swallow: bodies inside this are absorbed
    double isco_radius;         // r_isco: innermost stable circular orbit
    double viscosity_alpha;     // drag coefficient for disk gas
    double jet_speed;           // velocity of jet particles
    double jet_mass;            // mass per jet particle
    double jet_lifetime;        // seconds before jet particle dies
    double feedback_strength;   // multiplier on radiation pressure
    double eta_eff;             // radiative efficiency (luminosity = eta * mdot)
    double eddington_k;         // L_edd = k * M_smbh
    double accretion_smoothing; // exponential avg factor for accretion_rate
    int jet_cap;                // max jet particles alive at once
    int jet_count;              // current live jet particles (internal state)
    int max_bodies;             // capacity of body array (for jet spawning)
    int jet_ring_count;         // particles per jet cross-section ring (limb brightening)
} QuasarConfig;

QuasarConfig quasar_default_config(void);

// Run one quasar physics step. Called after octree force computation, before drift.
// bodies: array of bodies (may grow if jets spawn)
// n: pointer to current body count (updated if jets spawn or compaction occurs)
// n_alloc: allocated capacity of body array
// cfg: quasar configuration
// dt: timestep
void quasar_step(Body *bodies, int *n, int n_alloc, QuasarConfig *cfg, double dt);

// Compact dead bodies out of the array. Returns new count.
int quasar_compact(Body *bodies, int n);

/* Test-only wrapper */
void decay_jets_wrapper(Body *bodies, int *n, int n_alloc, QuasarConfig *cfg, double dt);

#endif
