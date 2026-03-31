#ifndef SPH_H
#define SPH_H

#include "body.h"
#include "octree.h"

#define SPH_GAMMA (5.0 / 3.0)
#define SPH_TARGET_NEIGHBORS 32
#define SPH_MAX_NEIGHBORS 128

/* Compute density and pressure for all BODY_GAS particles using octree. */
void sph_compute_density(Body *bodies, int n, const OctreeNode *tree);

/* Compute SPH accelerations (pressure gradient + artificial viscosity).
   Adds to ax/ay/az on top of existing gravity forces. */
void sph_compute_forces(Body *bodies, int n, const OctreeNode *tree);

/* Apply radiative cooling: reduces internal_energy toward a floor. */
void sph_apply_cooling(Body *bodies, int n, double dt);

#endif
