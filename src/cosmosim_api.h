#ifndef COSMOSIM_API_H
#define COSMOSIM_API_H

#include "body.h"

#ifdef _WIN32
#ifdef COSMOSIM_BUILDING_DLL
#define COSMOSIM_API __declspec(dllexport)
#else
#define COSMOSIM_API __declspec(dllimport)
#endif
#else
#define COSMOSIM_API __attribute__((visibility("default")))
#endif

typedef struct {
    int n_bodies;
    int merger;            /* 0 or 1 */
    int quasar;            /* 0 or 1 */
    double dt;             /* timestep */
    double theta;          /* Barnes-Hut opening angle */
    double smbh_mass_frac; /* SMBH mass fraction (quasar mode) */
    double accretion_radius;
    double jet_speed;
    double feedback_strength;
    int substeps; /* integration substeps per cosmosim_step call */
} CosmosimConfig;

typedef void *SimHandle;

#ifdef __cplusplus
extern "C" {
#endif

COSMOSIM_API CosmosimConfig cosmosim_default_config(void);
COSMOSIM_API SimHandle cosmosim_create(CosmosimConfig config);
COSMOSIM_API void cosmosim_step(SimHandle handle);
COSMOSIM_API const Body *cosmosim_get_bodies(SimHandle handle);
COSMOSIM_API int cosmosim_get_count(SimHandle handle);
COSMOSIM_API int cosmosim_get_active_count(SimHandle handle);
COSMOSIM_API double cosmosim_get_sim_time(SimHandle handle);
COSMOSIM_API void cosmosim_destroy(SimHandle handle);

#ifdef __cplusplus
}
#endif

#endif /* COSMOSIM_API_H */
