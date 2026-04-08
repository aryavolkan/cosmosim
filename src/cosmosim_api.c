#include <stdlib.h>
#include <string.h>

#include "cosmosim_api.h"
#include "octree.h"
#include "integrator.h"
#include "initial_conditions.h"
#include "quasar.h"

#define G          1.0
#define SOFTENING  0.5

/* Galaxy generation parameters */
#define GALAXY_MASS    100.0
#define DISK_RADIUS    15.0
#define SEPARATION     40.0
#define APPROACH_VEL   0.15

typedef struct {
    Body *bodies;
    OctreeNode *pool;
    int n_alloc;
    int current_n;
    CosmosimConfig config;
    QuasarConfig qcfg;
    double sim_time;
    int compact_counter;
} SimState;

COSMOSIM_API CosmosimConfig cosmosim_default_config(void)
{
    CosmosimConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.n_bodies = 20000;
    cfg.merger = 0;
    cfg.quasar = 0;
    cfg.dt = 0.005;
    cfg.theta = 0.5;
    cfg.smbh_mass_frac = 0.05;
    cfg.accretion_radius = 6.0;
    cfg.jet_speed = 15.0;
    cfg.feedback_strength = 0.3;
    cfg.substeps = 2;
    return cfg;
}

COSMOSIM_API SimHandle cosmosim_create(CosmosimConfig config)
{
    int n = config.n_bodies;
    if (n < 2) n = 2;

    int n_dust = (config.merger && config.quasar) ? n / 5 : 0;
    int n_alloc = config.quasar ? n + n_dust + n / 4 : n;

    Body *bodies = calloc(n_alloc, sizeof(Body));
    OctreeNode *pool = malloc(8 * (size_t)n_alloc * sizeof(OctreeNode));

    if (!bodies || !pool) {
        free(bodies);
        free(pool);
        return NULL;
    }

    /* Generate initial conditions */
    if (config.quasar) {
        if (config.merger) {
            generate_quasar_merger(bodies, n, SEPARATION, APPROACH_VEL, config.smbh_mass_frac);
            if (n_dust > 0)
                generate_merger_dust(bodies, n, n_dust, SEPARATION);
        } else {
            generate_quasar_galaxy(bodies, n,
                0.0, 0.0, (double)n * 2.0, DISK_RADIUS,
                0.0, 0.0, config.smbh_mass_frac);
        }
    } else {
        if (config.merger) {
            generate_merger(bodies, n, SEPARATION, APPROACH_VEL);
        } else {
            generate_spiral_galaxy(bodies, n,
                0.0, 0.0, GALAXY_MASS, DISK_RADIUS, 0.0, 0.0);
        }
    }

    int initial_n = n + n_dust;
    integrator_init_accelerations(bodies, initial_n, G, SOFTENING, config.theta, pool);

    /* Set up quasar config */
    QuasarConfig qcfg = quasar_default_config();
    if (config.quasar) {
        qcfg.accretion_radius = config.accretion_radius;
        qcfg.jet_speed = config.jet_speed;
        qcfg.feedback_strength = config.feedback_strength;
        qcfg.jet_cap = n / 10;
        qcfg.max_bodies = n_alloc;
    }

    SimState *state = malloc(sizeof(SimState));
    if (!state) {
        free(bodies);
        free(pool);
        return NULL;
    }

    state->bodies = bodies;
    state->pool = pool;
    state->n_alloc = n_alloc;
    state->current_n = initial_n;
    state->config = config;
    state->qcfg = qcfg;
    state->sim_time = 0.0;
    state->compact_counter = 0;

    return (SimHandle)state;
}

COSMOSIM_API void cosmosim_step(SimHandle handle)
{
    if (!handle) return;
    SimState *s = (SimState *)handle;

    for (int sub = 0; sub < s->config.substeps; sub++) {
        integrator_step(s->bodies, s->current_n, s->config.dt,
                        G, SOFTENING, s->config.theta, s->pool,
                        s->config.quasar);
        if (s->config.quasar) {
            quasar_step(s->bodies, &s->current_n, s->n_alloc,
                        &s->qcfg, s->config.dt);
        }
    }

    if (s->config.quasar) {
        s->compact_counter++;
        if (s->compact_counter >= 60) {
            s->current_n = quasar_compact(s->bodies, s->current_n);
            s->compact_counter = 0;
        }
    }

    s->sim_time += s->config.dt * s->config.substeps;
}

COSMOSIM_API const Body* cosmosim_get_bodies(SimHandle handle)
{
    if (!handle) return NULL;
    return ((SimState *)handle)->bodies;
}

COSMOSIM_API int cosmosim_get_count(SimHandle handle)
{
    if (!handle) return 0;
    return ((SimState *)handle)->current_n;
}

COSMOSIM_API int cosmosim_get_active_count(SimHandle handle)
{
    if (!handle) return 0;
    SimState *s = (SimState *)handle;
    int count = 0;
    for (int i = 0; i < s->current_n; i++) {
        if (s->bodies[i].mass > 0.0)
            count++;
    }
    return count;
}

COSMOSIM_API double cosmosim_get_sim_time(SimHandle handle)
{
    if (!handle) return 0.0;
    return ((SimState *)handle)->sim_time;
}

COSMOSIM_API void cosmosim_destroy(SimHandle handle)
{
    if (!handle) return;
    SimState *s = (SimState *)handle;
    free(s->bodies);
    free(s->pool);
    free(s);
}
