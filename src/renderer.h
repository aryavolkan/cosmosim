#ifndef RENDERER_H
#define RENDERER_H

#include "body.h"

typedef struct {
    float azimuth;
    float elevation;
    float distance;
    float target_x, target_y, target_z;
} Camera;

#define MAX_SMBH 2

typedef struct {
    float x, y, z;
    float luminosity;
    float mass;
    float spin_x, spin_y, spin_z; // normalized spin axis (for frame-dragging beaming)
} SMBHInfo;

typedef struct {
    int hdr_enabled;
    float smbh_x, smbh_y, smbh_z; // primary SMBH (backwards compat)
    float smbh_luminosity;
    float smbh_mass;
    SMBHInfo smbhs[MAX_SMBH];
    int smbh_count;
    int bloom_iterations;
    int lensing_samples;
    float exposure;
} RendererConfig;

int renderer_init(const RendererConfig *rcfg);
void renderer_update_smbh(RendererConfig *rcfg, const Body *bodies, int n);
void renderer_draw(const Body *bodies,
                   int n,
                   const Camera *cam,
                   int window_width,
                   int window_height,
                   const RendererConfig *rcfg);
void renderer_cleanup(void);

#endif
