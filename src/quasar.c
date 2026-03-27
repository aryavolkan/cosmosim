#include "quasar.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// xorshift64 PRNG for jet spawning
static uint64_t qrng_state = 1;

static uint64_t qrng_next(void)
{
    uint64_t x = qrng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    qrng_state = x;
    return x;
}

static double qrng_uniform(void)
{
    return (qrng_next() >> 11) * (1.0 / 9007199254740992.0);
}

static double qrng_gaussian(void)
{
    double u1 = qrng_uniform();
    double u2 = qrng_uniform();
    if (u1 < 1e-15) u1 = 1e-15;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

QuasarConfig quasar_default_config(void)
{
    QuasarConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.accretion_radius = 3.0;
    cfg.swallow_radius = 0.3;
    cfg.isco_radius = 0.5;
    cfg.viscosity_alpha = 0.05;
    cfg.jet_speed = 20.0;
    cfg.jet_mass = 0.5;
    cfg.jet_lifetime = 2.0;
    cfg.feedback_strength = 1.0;
    cfg.eta_eff = 0.1;
    cfg.eddington_k = 0.5;
    cfg.accretion_smoothing = 0.95;
    cfg.jet_cap = 2000;
    cfg.jet_count = 0;
    cfg.max_bodies = 0;
    return cfg;
}

static void accrete_body(Body *smbh, Body *victim)
{
    // Conservation of momentum
    double new_mass = smbh->mass + victim->mass;
    smbh->vx = (smbh->mass * smbh->vx + victim->mass * victim->vx) / new_mass;
    smbh->vy = (smbh->mass * smbh->vy + victim->mass * victim->vy) / new_mass;
    smbh->vz = (smbh->mass * smbh->vz + victim->mass * victim->vz) / new_mass;

    // Nudge spin axis from accreted angular momentum
    double rx = victim->x - smbh->x;
    double ry = victim->y - smbh->y;
    double rz = victim->z - smbh->z;
    double rvx = victim->vx - smbh->vx;
    double rvy = victim->vy - smbh->vy;
    double rvz = victim->vz - smbh->vz;
    double lx = ry * rvz - rz * rvy;
    double ly = rz * rvx - rx * rvz;
    double lz = rx * rvy - ry * rvx;
    double l_mag = sqrt(lx * lx + ly * ly + lz * lz);

    if (l_mag > 1e-15) {
        double weight = victim->mass / new_mass * 0.01;
        smbh->spin_x += weight * lx / l_mag;
        smbh->spin_y += weight * ly / l_mag;
        smbh->spin_z += weight * lz / l_mag;
        double s_mag = sqrt(smbh->spin_x * smbh->spin_x +
                            smbh->spin_y * smbh->spin_y +
                            smbh->spin_z * smbh->spin_z);
        if (s_mag > 1e-15) {
            smbh->spin_x /= s_mag;
            smbh->spin_y /= s_mag;
            smbh->spin_z /= s_mag;
        }
    }

    smbh->mass = new_mass;

    // Kill victim
    victim->mass = 0.0;
    victim->vx = victim->vy = victim->vz = 0.0;
    victim->ax = victim->ay = victim->az = 0.0;
}

static void apply_disk_drag(const Body *smbh, Body *gas, double alpha)
{
    double rx = gas->x - smbh->x;
    double ry = gas->y - smbh->y;
    double rz = gas->z - smbh->z;
    double r = sqrt(rx * rx + ry * ry + rz * rz);
    if (r < 1e-15) return;

    double rvx = gas->vx - smbh->vx;
    double rvy = gas->vy - smbh->vy;
    double rvz = gas->vz - smbh->vz;

    double v_dot_r = (rvx * rx + rvy * ry + rvz * rz) / r;
    double vr_x = v_dot_r * rx / r;
    double vr_y = v_dot_r * ry / r;
    double vr_z = v_dot_r * rz / r;

    gas->ax -= alpha * vr_x;
    gas->ay -= alpha * vr_y;
    gas->az -= alpha * vr_z;
}

static void process_accretion(Body *bodies, int n, const QuasarConfig *cfg, double dt,
                              double *mass_swallowed)
{
    (void)dt;
    *mass_swallowed = 0.0;
    double r_acc_sq = cfg->accretion_radius * cfg->accretion_radius;
    double r_swallow_sq = cfg->swallow_radius * cfg->swallow_radius;

    for (int s = 0; s < n; s++) {
        if (bodies[s].type != BODY_SMBH || bodies[s].mass <= 0.0) continue;
        Body *smbh = &bodies[s];

        for (int i = 0; i < n; i++) {
            if (i == s || bodies[i].mass <= 0.0) continue;
            if (bodies[i].type == BODY_SMBH) continue;

            double dx = bodies[i].x - smbh->x;
            double dy = bodies[i].y - smbh->y;
            double dz = bodies[i].z - smbh->z;
            double dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq > r_acc_sq) continue;

            if (dist_sq < r_swallow_sq) {
                *mass_swallowed += bodies[i].mass;
                accrete_body(smbh, &bodies[i]);
                continue;
            }

            double r = sqrt(dist_sq);
            double rvx = bodies[i].vx - smbh->vx;
            double rvy = bodies[i].vy - smbh->vy;
            double rvz = bodies[i].vz - smbh->vz;
            double lx = dy * rvz - dz * rvy;
            double ly = dz * rvx - dx * rvz;
            double lz = dx * rvy - dy * rvx;
            double l_sq = lx * lx + ly * ly + lz * lz;
            double r_circ = l_sq / (smbh->mass * r);

            if (r_circ > cfg->isco_radius && bodies[i].type != BODY_GAS) {
                bodies[i].type = BODY_GAS;
            }

            if (bodies[i].type == BODY_GAS) {
                apply_disk_drag(smbh, &bodies[i], cfg->viscosity_alpha);
                if (r < cfg->isco_radius) {
                    *mass_swallowed += bodies[i].mass;
                    accrete_body(smbh, &bodies[i]);
                }
            }
        }
    }
}

static void apply_feedback(Body *bodies, int n, const QuasarConfig *cfg)
{
    double r_fb_sq = (cfg->accretion_radius * 3.0) * (cfg->accretion_radius * 3.0);

    for (int s = 0; s < n; s++) {
        if (bodies[s].type != BODY_SMBH || bodies[s].luminosity <= 0.0) continue;
        const Body *smbh = &bodies[s];

        for (int i = 0; i < n; i++) {
            if (i == s || bodies[i].mass <= 0.0 || bodies[i].type == BODY_SMBH) continue;

            double dx = bodies[i].x - smbh->x;
            double dy = bodies[i].y - smbh->y;
            double dz = bodies[i].z - smbh->z;
            double dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq > r_fb_sq || dist_sq < 1e-10) continue;

            double r = sqrt(dist_sq);
            double f_mag = cfg->feedback_strength * smbh->luminosity / (4.0 * M_PI * dist_sq);

            bodies[i].ax += f_mag * dx / r;
            bodies[i].ay += f_mag * dy / r;
            bodies[i].az += f_mag * dz / r;
        }
    }
}

static void spawn_jets(Body *bodies, int *n, int n_alloc, QuasarConfig *cfg)
{
    for (int s = 0; s < *n; s++) {
        if (bodies[s].type != BODY_SMBH || bodies[s].luminosity <= 0.0) continue;
        const Body *smbh = &bodies[s];

        double jet_energy = 0.5 * cfg->jet_mass * cfg->jet_speed * cfg->jet_speed;
        if (jet_energy < 1e-15) continue;
        int n_spawn = (int)(smbh->luminosity / jet_energy);
        if (n_spawn < 1) n_spawn = 1;
        if (n_spawn > 4) n_spawn = 4;

        for (int j = 0; j < n_spawn && cfg->jet_count < cfg->jet_cap; j++) {
            if (*n >= n_alloc) break;

            double sign = (j % 2 == 0) ? 1.0 : -1.0;

            double cone_angle = 0.1;
            double perp_x, perp_y, perp_z;
            if (fabs(smbh->spin_z) < 0.9) {
                perp_x = -smbh->spin_y;
                perp_y =  smbh->spin_x;
                perp_z =  0.0;
            } else {
                perp_x =  0.0;
                perp_y = -smbh->spin_z;
                perp_z =  smbh->spin_y;
            }
            double pmag = sqrt(perp_x * perp_x + perp_y * perp_y + perp_z * perp_z);
            if (pmag > 1e-15) { perp_x /= pmag; perp_y /= pmag; perp_z /= pmag; }

            double angle = qrng_uniform() * 2.0 * M_PI;
            double offset = qrng_gaussian() * cone_angle;

            Body *jet = &bodies[*n];
            memset(jet, 0, sizeof(Body));
            jet->type = BODY_JET;
            jet->mass = cfg->jet_mass;
            jet->lifetime = cfg->jet_lifetime;

            jet->x = smbh->x + sign * smbh->spin_x * cfg->swallow_radius +
                     offset * (perp_x * cos(angle));
            jet->y = smbh->y + sign * smbh->spin_y * cfg->swallow_radius +
                     offset * (perp_y * cos(angle));
            jet->z = smbh->z + sign * smbh->spin_z * cfg->swallow_radius +
                     offset * (perp_z * sin(angle));

            jet->vx = smbh->vx + sign * smbh->spin_x * cfg->jet_speed;
            jet->vy = smbh->vy + sign * smbh->spin_y * cfg->jet_speed;
            jet->vz = smbh->vz + sign * smbh->spin_z * cfg->jet_speed;

            (*n)++;
            cfg->jet_count++;
        }
    }
}

static void decay_jets(Body *bodies, int n, QuasarConfig *cfg, double dt)
{
    for (int i = 0; i < n; i++) {
        if (bodies[i].type != BODY_JET || bodies[i].mass <= 0.0) continue;
        bodies[i].lifetime -= dt;
        if (bodies[i].lifetime <= 0.0) {
            bodies[i].mass = 0.0;
            bodies[i].vx = bodies[i].vy = bodies[i].vz = 0.0;
            bodies[i].ax = bodies[i].ay = bodies[i].az = 0.0;
            cfg->jet_count--;
        }
    }
}

static void recycle_distant_jets(Body *bodies, int n, QuasarConfig *cfg)
{
    // Find first SMBH
    int smbh_idx = -1;
    for (int i = 0; i < n; i++) {
        if (bodies[i].type == BODY_SMBH && bodies[i].mass > 0.0) {
            smbh_idx = i;
            break;
        }
    }
    if (smbh_idx < 0) return;

    const Body *smbh = &bodies[smbh_idx];
    double recycle_r = cfg->accretion_radius * 10.0;
    double recycle_r_sq = recycle_r * recycle_r;

    for (int i = 0; i < n; i++) {
        if (bodies[i].type != BODY_JET || bodies[i].mass <= 0.0) continue;

        double dx = bodies[i].x - smbh->x;
        double dy = bodies[i].y - smbh->y;
        double dz = bodies[i].z - smbh->z;
        double dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < recycle_r_sq) continue;

        // Recycle: place at accretion boundary with inward velocity
        double angle = 2.0 * M_PI * qrng_uniform();
        double cos_phi = 2.0 * qrng_uniform() - 1.0;
        double sin_phi = sqrt(1.0 - cos_phi * cos_phi);
        double r_place = cfg->accretion_radius;

        bodies[i].x = smbh->x + r_place * sin_phi * cos(angle);
        bodies[i].y = smbh->y + r_place * sin_phi * sin(angle);
        bodies[i].z = smbh->z + r_place * cos_phi;

        // Inward radial velocity at half circular speed
        double v_inward = 0.5 * sqrt(smbh->mass / r_place);
        double nx = (bodies[i].x - smbh->x) / r_place;
        double ny = (bodies[i].y - smbh->y) / r_place;
        double nz = (bodies[i].z - smbh->z) / r_place;
        bodies[i].vx = smbh->vx - v_inward * nx;
        bodies[i].vy = smbh->vy - v_inward * ny;
        bodies[i].vz = smbh->vz - v_inward * nz;

        bodies[i].ax = bodies[i].ay = bodies[i].az = 0.0;
        bodies[i].type = BODY_GAS;
        bodies[i].lifetime = 0.0;
        cfg->jet_count--;
    }
}

void quasar_step(Body *bodies, int *n, int n_alloc, QuasarConfig *cfg, double dt)
{
    // 1. Process accretion
    double mass_swallowed = 0.0;
    process_accretion(bodies, *n, cfg, dt, &mass_swallowed);

    // 2. Update SMBH accretion rate and luminosity
    for (int i = 0; i < *n; i++) {
        if (bodies[i].type != BODY_SMBH) continue;
        double mdot_instant = mass_swallowed / dt;
        bodies[i].accretion_rate = cfg->accretion_smoothing * bodies[i].accretion_rate +
                                   (1.0 - cfg->accretion_smoothing) * mdot_instant;

        double l_edd = cfg->eddington_k * bodies[i].mass;
        bodies[i].luminosity = cfg->eta_eff * bodies[i].accretion_rate;
        if (bodies[i].luminosity > l_edd) {
            bodies[i].luminosity = l_edd;
        }
    }

    // 3. Apply AGN feedback
    apply_feedback(bodies, *n, cfg);

    // 4. Decay expired jet particles
    decay_jets(bodies, *n, cfg, dt);

    // 4b. Recycle distant jet particles as infalling gas
    recycle_distant_jets(bodies, *n, cfg);

    // 5. Spawn new jet particles
    spawn_jets(bodies, n, n_alloc, cfg);
}

int quasar_compact(Body *bodies, int n)
{
    int write = 0;
    for (int read = 0; read < n; read++) {
        if (bodies[read].mass > 0.0) {
            if (write != read) {
                bodies[write] = bodies[read];
            }
            write++;
        }
    }
    return write;
}
