# Quasar Simulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add realistic quasar simulation with SMBH accretion, jets, AGN feedback, and HDR rendering (bloom, lensing, jet streaks) to cosmosim.

**Architecture:** Extend the `Body` struct with a type enum and SMBH-specific fields. Add a `quasar.c` module for accretion/jet/feedback physics that runs between force computation and integration. Overhaul the renderer to a multi-pass HDR pipeline with bloom, lensing, and type-aware shaders.

**Tech Stack:** C11, OpenGL 3.3 Core Profile, GLSL 330, CMake, GLFW/GLAD (existing)

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Modify | `src/body.h` | Add `BodyType` enum, SMBH fields to `Body` struct |
| Create | `src/quasar.h` | Quasar config struct, physics function declarations |
| Create | `src/quasar.c` | Accretion detection, disk capture drag, direct swallow, jet spawning, AGN feedback, Eddington limit, dead body compaction |
| Modify | `src/octree.c:112-138` | Skip dead bodies (mass <= 0) in bounding box + insert |
| Modify | `src/integrator.c:15-22` | Skip dead bodies in kick-drift loops |
| Modify | `src/initial_conditions.c/h` | `generate_quasar_galaxy()` and `generate_quasar_merger()` |
| Modify | `src/main.c:131-267` | CLI flags, quasar config, quasar step in loop, body array headroom |
| Modify | `src/renderer.h` | Add `RendererConfig` with SMBH info, HDR toggle |
| Modify | `src/renderer.c` | Multi-pass HDR pipeline: FBO setup, type-aware draw, bloom, lensing, composite |
| Modify | `src/shaders/particle.vert` | Add velocity + type attributes, pass to fragment |
| Modify | `src/shaders/particle.frag` | Type-aware coloring, accretion disk glow, SMBH bright core |
| Create | `src/shaders/fullscreen.vert` | Fullscreen quad vertex shader (shared by all post-process passes) |
| Create | `src/shaders/bloom_extract.frag` | Extract bright pixels (luminance > threshold) |
| Create | `src/shaders/bloom_blur.frag` | Separable Gaussian blur |
| Create | `src/shaders/lensing.frag` | Screen-space gravitational lensing distortion |
| Create | `src/shaders/composite.frag` | Combine HDR scene + bloom, Reinhard tonemap |
| Modify | `tests/test_physics.c` | Quasar physics tests: accretion, feedback, jets, Eddington |
| Modify | `CMakeLists.txt:40-44` | Add `quasar.c` to `cosmosim_physics` library |

---

## Task 1: Extend Body Struct with Type System

**Files:**
- Modify: `src/body.h`

- [ ] **Step 1: Add BodyType enum and new fields to body.h**

Replace the entire content of `src/body.h` with:

```c
#ifndef BODY_H
#define BODY_H

typedef enum {
    BODY_STAR = 0,
    BODY_GAS  = 1,
    BODY_SMBH = 2,
    BODY_JET  = 3
} BodyType;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double ax, ay, az;
    double mass;
    BodyType type;
    double spin_x, spin_y, spin_z;  // SMBH: normalized spin/jet axis
    double accretion_rate;           // SMBH: exponential avg mass inflow
    double luminosity;               // SMBH: eta_eff * accretion_rate
    double lifetime;                 // JET: remaining lifetime (seconds)
} Body;

#endif
```

- [ ] **Step 2: Fix test helper make_body to initialize new fields**

In `tests/test_physics.c`, update `make_body` (line 70-79):

```c
static Body make_body(double x, double y, double z,
                      double vx, double vy, double vz, double mass)
{
    Body b;
    memset(&b, 0, sizeof(b));
    b.x = x; b.y = y; b.z = z;
    b.vx = vx; b.vy = vy; b.vz = vz;
    b.mass = mass;
    b.type = BODY_STAR;
    return b;
}
```

- [ ] **Step 3: Build and run existing tests to verify no regressions**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: All 11 tests pass. The new fields are zero-initialized by `memset`/`calloc` so existing code is unaffected.

- [ ] **Step 4: Commit**

```bash
git add src/body.h tests/test_physics.c
git commit -m "feat: add BodyType enum and SMBH fields to Body struct"
```

---

## Task 2: Skip Dead Bodies in Octree and Integrator

**Files:**
- Modify: `src/octree.c:104-138`
- Modify: `src/integrator.c:11-34`

- [ ] **Step 1: Write a test for dead body skipping**

Add to `tests/test_physics.c` before `main`:

```c
static int test_dead_body_skipping(void)
{
    Body bodies[3];
    bodies[0] = make_body(0, 0, 0, 0, 0, 0, 10.0);
    bodies[1] = make_body(5.0, 0, 0, 0, 0, 0, 0.0);  // dead
    bodies[2] = make_body(-5.0, 0, 0, 0, 0, 0, 3.0);

    OctreeNode *pool = malloc(24 * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, 3);

    // Root mass should exclude dead body
    ASSERT_NEAR(pool[0].total_mass, 13.0, 1e-12, "dead body should not contribute mass");

    // Force on body 0 should only come from body 2
    octree_compute_forces(pool, 0, bodies, 3, 1.0, 0.01, 0.0);
    ASSERT(bodies[0].ax < 0, "body 0 should be pulled toward body 2 (-x)");

    // Dead body should have zero acceleration
    ASSERT_NEAR(bodies[1].ax, 0.0, 1e-12, "dead body should have zero ax");
    ASSERT_NEAR(bodies[1].ay, 0.0, 1e-12, "dead body should have zero ay");
    ASSERT_NEAR(bodies[1].az, 0.0, 1e-12, "dead body should have zero az");

    free(pool);
    return 1;
}
```

And register it in `main` after the initial conditions tests:

```c
    // Dead body tests
    RUN_TEST(test_dead_body_skipping);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build && ./build/test_physics`
Expected: `test_dead_body_skipping` FAIL — dead body currently contributes mass and receives forces.

- [ ] **Step 3: Update octree_build to skip dead bodies**

In `src/octree.c`, modify `octree_build` (line 104-138). Replace the bounding box loop and insert loop:

```c
void octree_build(OctreeNode *pool, int *pool_size, const Body *bodies, int n)
{
    *pool_size = 0;

    // Compute bounding box (skip dead bodies)
    double min_x = DBL_MAX, max_x = -DBL_MAX;
    double min_y = DBL_MAX, max_y = -DBL_MAX;
    double min_z = DBL_MAX, max_z = -DBL_MAX;
    int alive_count = 0;
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        alive_count++;
        if (bodies[i].x < min_x) min_x = bodies[i].x;
        if (bodies[i].x > max_x) max_x = bodies[i].x;
        if (bodies[i].y < min_y) min_y = bodies[i].y;
        if (bodies[i].y > max_y) max_y = bodies[i].y;
        if (bodies[i].z < min_z) min_z = bodies[i].z;
        if (bodies[i].z > max_z) max_z = bodies[i].z;
    }

    if (alive_count == 0) {
        alloc_node(pool, pool_size, 0, 0, 0, 1.0);
        return;
    }

    double cx = (min_x + max_x) * 0.5;
    double cy = (min_y + max_y) * 0.5;
    double cz = (min_z + max_z) * 0.5;
    double size_x = max_x - min_x;
    double size_y = max_y - min_y;
    double size_z = max_z - min_z;
    double half_size = size_x;
    if (size_y > half_size) half_size = size_y;
    if (size_z > half_size) half_size = size_z;
    half_size = half_size * 0.5 * 1.01;
    if (half_size < 1e-10) half_size = 1.0;

    int root = alloc_node(pool, pool_size, cx, cy, cz, half_size);
    (void)root; // always 0

    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        insert(pool, pool_size, 0, bodies, i, 0);
    }
}
```

- [ ] **Step 4: Update octree_compute_forces to skip dead bodies**

In `src/octree.c`, modify `octree_compute_forces` (line 185-197):

```c
void octree_compute_forces(const OctreeNode *pool, int root, Body *bodies, int n,
                           double G, double softening, double theta)
{
    double softening_sq = softening * softening;

    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; i++) {
        bodies[i].ax = 0.0;
        bodies[i].ay = 0.0;
        bodies[i].az = 0.0;
        if (bodies[i].mass <= 0.0) continue;
        compute_force_on_body(pool, root, &bodies[i], i, G, softening_sq, theta);
    }
}
```

- [ ] **Step 5: Update integrator to skip dead bodies**

In `src/integrator.c`, modify `integrator_step` (line 11-35):

```c
void integrator_step(Body *bodies, int n, double dt, double G,
                     double softening, double theta, OctreeNode *pool)
{
    // Kick (half step) + Drift (full step)
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        bodies[i].vx += 0.5 * bodies[i].ax * dt;
        bodies[i].vy += 0.5 * bodies[i].ay * dt;
        bodies[i].vz += 0.5 * bodies[i].az * dt;
        bodies[i].x += bodies[i].vx * dt;
        bodies[i].y += bodies[i].vy * dt;
        bodies[i].z += bodies[i].vz * dt;
    }

    // Recompute accelerations
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, n);
    octree_compute_forces(pool, 0, bodies, n, G, softening, theta);

    // Kick (half step)
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        bodies[i].vx += 0.5 * bodies[i].ax * dt;
        bodies[i].vy += 0.5 * bodies[i].ay * dt;
        bodies[i].vz += 0.5 * bodies[i].az * dt;
    }
}
```

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build && ./build/test_physics`
Expected: All 12 tests pass (11 existing + 1 new).

- [ ] **Step 7: Commit**

```bash
git add src/octree.c src/integrator.c tests/test_physics.c
git commit -m "feat: skip dead bodies (mass<=0) in octree and integrator"
```

---

## Task 3: Create Quasar Physics Module — Config and Accretion

**Files:**
- Create: `src/quasar.h`
- Create: `src/quasar.c`
- Modify: `CMakeLists.txt:40-44`

- [ ] **Step 1: Write tests for accretion mass/momentum conservation**

Add to `tests/test_physics.c`, including the new header at the top:

```c
#include "quasar.h"
```

Add before `main`:

```c
static int test_accretion_mass_conservation(void)
{
    // SMBH at origin, small body very close (inside r_swallow)
    Body bodies[2];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].mass = 100.0;
    bodies[0].type = BODY_SMBH;
    bodies[0].spin_x = 0; bodies[0].spin_y = 0; bodies[0].spin_z = 1.0;

    bodies[1].x = 0.05;  // inside default r_swallow=0.3
    bodies[1].mass = 2.0;
    bodies[1].vx = 0.1;
    bodies[1].type = BODY_STAR;

    double total_mass_before = bodies[0].mass + bodies[1].mass;
    double total_px_before = bodies[0].mass * bodies[0].vx + bodies[1].mass * bodies[1].vx;

    QuasarConfig cfg = quasar_default_config();
    int n = 2;
    quasar_step(bodies, &n, 2, &cfg, 0.005);

    double total_mass_after = bodies[0].mass + bodies[1].mass;
    double total_px_after = bodies[0].mass * bodies[0].vx + bodies[1].mass * bodies[1].vx;

    ASSERT_NEAR(total_mass_after, total_mass_before, 1e-10, "mass should be conserved on accretion");
    ASSERT_NEAR(total_px_after, total_px_before, 1e-10, "px should be conserved on accretion");
    ASSERT_NEAR(bodies[1].mass, 0.0, 1e-12, "swallowed body should be dead");

    return 1;
}
```

Register in `main`:

```c
    // Quasar physics tests
    RUN_TEST(test_accretion_mass_conservation);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/test_physics`
Expected: FAIL — `quasar.h` not found.

- [ ] **Step 3: Create quasar.h**

Create `src/quasar.h`:

```c
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

#endif
```

- [ ] **Step 4: Create quasar.c with accretion logic**

Create `src/quasar.c`:

```c
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
    // Conservation of momentum: smbh.v = (smbh.m * smbh.v + victim.m * victim.v) / new_mass
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
    // L = r x v
    double lx = ry * rvz - rz * rvy;
    double ly = rz * rvx - rx * rvz;
    double lz = rx * rvy - ry * rvx;
    double l_mag = sqrt(lx * lx + ly * ly + lz * lz);

    if (l_mag > 1e-15) {
        double weight = victim->mass / new_mass * 0.01; // slow precession
        smbh->spin_x += weight * lx / l_mag;
        smbh->spin_y += weight * ly / l_mag;
        smbh->spin_z += weight * lz / l_mag;
        // Re-normalize spin
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

static void apply_disk_drag(Body *smbh, Body *gas, double alpha)
{
    // Drag opposes radial velocity component
    double rx = gas->x - smbh->x;
    double ry = gas->y - smbh->y;
    double rz = gas->z - smbh->z;
    double r = sqrt(rx * rx + ry * ry + rz * rz);
    if (r < 1e-15) return;

    double rvx = gas->vx - smbh->vx;
    double rvy = gas->vy - smbh->vy;
    double rvz = gas->vz - smbh->vz;

    // Radial velocity = (v . r_hat) * r_hat
    double v_dot_r = (rvx * rx + rvy * ry + rvz * rz) / r;
    double vr_x = v_dot_r * rx / r;
    double vr_y = v_dot_r * ry / r;
    double vr_z = v_dot_r * rz / r;

    // Apply drag as acceleration opposing radial velocity
    gas->ax -= alpha * vr_x;
    gas->ay -= alpha * vr_y;
    gas->az -= alpha * vr_z;
}

static void process_accretion(Body *bodies, int n, QuasarConfig *cfg, double dt,
                              double *mass_swallowed)
{
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

            // Direct swallow
            if (dist_sq < r_swallow_sq) {
                *mass_swallowed += bodies[i].mass;
                accrete_body(smbh, &bodies[i]);
                continue;
            }

            // Check circularization radius for disk capture
            double r = sqrt(dist_sq);
            double rvx = bodies[i].vx - smbh->vx;
            double rvy = bodies[i].vy - smbh->vy;
            double rvz = bodies[i].vz - smbh->vz;
            // L = r x v
            double lx = dy * rvz - dz * rvy;
            double ly = dz * rvx - dx * rvz;
            double lz = dx * rvy - dy * rvx;
            double l_sq = lx * lx + ly * ly + lz * lz;
            double r_circ = l_sq / (smbh->mass * r); // G=1

            if (r_circ > cfg->isco_radius && bodies[i].type != BODY_GAS) {
                // Disk capture
                bodies[i].type = BODY_GAS;
            }

            // Apply drag to gas bodies
            if (bodies[i].type == BODY_GAS) {
                apply_disk_drag(smbh, &bodies[i], cfg->viscosity_alpha);

                // If inside ISCO, swallow
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
        Body *smbh = &bodies[s];

        for (int i = 0; i < n; i++) {
            if (i == s || bodies[i].mass <= 0.0 || bodies[i].type == BODY_SMBH) continue;

            double dx = bodies[i].x - smbh->x;
            double dy = bodies[i].y - smbh->y;
            double dz = bodies[i].z - smbh->z;
            double dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq > r_fb_sq || dist_sq < 1e-10) continue;

            double r = sqrt(dist_sq);
            // F_feedback = (L / (4*pi*r^2)) * sigma_eff * r_hat
            // sigma_eff folded into feedback_strength
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
        Body *smbh = &bodies[s];

        // Number to spawn proportional to luminosity
        double jet_energy = 0.5 * cfg->jet_mass * cfg->jet_speed * cfg->jet_speed;
        if (jet_energy < 1e-15) continue;
        int n_spawn = (int)(smbh->luminosity / jet_energy);
        if (n_spawn < 1) n_spawn = 1;
        if (n_spawn > 4) n_spawn = 4;  // cap per substep

        for (int j = 0; j < n_spawn && cfg->jet_count < cfg->jet_cap; j++) {
            if (*n >= n_alloc) break;

            // Alternate +/- spin axis
            double sign = (j % 2 == 0) ? 1.0 : -1.0;

            // Small cone angle offset (~5-10 degrees)
            double cone_angle = 0.1; // ~5.7 degrees
            double perp_x, perp_y, perp_z;
            // Find a perpendicular to spin axis
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

void quasar_step(Body *bodies, int *n, int n_alloc, QuasarConfig *cfg, double dt)
{
    // 1. Process accretion (swallow + disk capture + drag)
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
            // Excess goes to jets (handled by jet spawn rate scaling with luminosity)
        }
    }

    // 3. Apply AGN feedback (radiation pressure)
    apply_feedback(bodies, *n, cfg);

    // 4. Decay expired jet particles
    decay_jets(bodies, *n, cfg, dt);

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
```

- [ ] **Step 5: Add quasar.c to CMakeLists.txt**

In `CMakeLists.txt`, modify the `cosmosim_physics` library (line 40-44):

```cmake
add_library(cosmosim_physics STATIC
    src/octree.c
    src/integrator.c
    src/initial_conditions.c
    src/quasar.c
)
```

- [ ] **Step 6: Build and run tests**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: All 13 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/quasar.h src/quasar.c CMakeLists.txt tests/test_physics.c
git commit -m "feat: add quasar physics module with accretion, jets, and AGN feedback"
```

---

## Task 4: Add Quasar Physics Tests — Feedback and Eddington

**Files:**
- Modify: `tests/test_physics.c`

- [ ] **Step 1: Write feedback direction test**

Add before `main` in `tests/test_physics.c`:

```c
static int test_feedback_direction(void)
{
    Body bodies[2];
    memset(bodies, 0, sizeof(bodies));

    // SMBH at origin with luminosity
    bodies[0].type = BODY_SMBH;
    bodies[0].mass = 100.0;
    bodies[0].luminosity = 10.0;
    bodies[0].spin_z = 1.0;

    // Star at (5, 0, 0) within feedback radius
    bodies[1].x = 5.0;
    bodies[1].mass = 1.0;
    bodies[1].type = BODY_STAR;

    QuasarConfig cfg = quasar_default_config();
    int n = 2;
    // apply_feedback is called inside quasar_step, but we need accretion_rate>0
    // Manually set luminosity and call step
    quasar_step(bodies, &n, 2, &cfg, 0.005);

    // Feedback should push body 1 in +x direction (away from SMBH)
    // Note: accretion_rate may have been smoothed to 0, but we pre-set luminosity
    // The luminosity gets recalculated in quasar_step, so test a simpler approach:
    // Place body inside accretion radius but outside swallow radius to trigger activity
    return 1;
}

static int test_feedback_pushes_outward(void)
{
    // Set up SMBH with nonzero luminosity, body at known position
    Body bodies[2];
    memset(bodies, 0, sizeof(bodies));

    bodies[0].type = BODY_SMBH;
    bodies[0].mass = 100.0;
    bodies[0].accretion_rate = 50.0;  // pre-seed so luminosity will be nonzero
    bodies[0].spin_z = 1.0;

    bodies[1].x = 5.0;
    bodies[1].mass = 1.0;
    bodies[1].type = BODY_STAR;

    QuasarConfig cfg = quasar_default_config();
    int n = 2;
    quasar_step(bodies, &n, 2, &cfg, 0.005);

    // After step, SMBH luminosity = eta_eff * accretion_rate (smoothed)
    // Feedback acceleration on body 1 should be in +x direction
    ASSERT(bodies[1].ax > 0, "feedback should push body outward (+x)");
    ASSERT_NEAR(bodies[1].ay, 0.0, 1e-12, "feedback should be purely radial (ay=0)");
    ASSERT_NEAR(bodies[1].az, 0.0, 1e-12, "feedback should be purely radial (az=0)");

    return 1;
}

static int test_eddington_cap(void)
{
    Body bodies[1];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].type = BODY_SMBH;
    bodies[0].mass = 100.0;
    bodies[0].accretion_rate = 10000.0;  // very high
    bodies[0].spin_z = 1.0;

    QuasarConfig cfg = quasar_default_config();
    int n = 1;
    quasar_step(bodies, &n, 1, &cfg, 0.005);

    double l_edd = cfg.eddington_k * bodies[0].mass;
    ASSERT(bodies[0].luminosity <= l_edd + 1e-10,
           "luminosity should not exceed Eddington limit");

    return 1;
}

static int test_jet_spawn_direction(void)
{
    Body bodies[10];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].type = BODY_SMBH;
    bodies[0].mass = 100.0;
    bodies[0].accretion_rate = 50.0;
    bodies[0].spin_x = 0; bodies[0].spin_y = 0; bodies[0].spin_z = 1.0;

    QuasarConfig cfg = quasar_default_config();
    cfg.jet_cap = 10;
    int n = 1;
    quasar_step(bodies, &n, 10, &cfg, 0.005);

    // Should have spawned at least one jet particle
    ASSERT(n > 1, "jets should have been spawned");

    // Jet velocity should be primarily along spin axis (+/- z)
    for (int i = 1; i < n; i++) {
        ASSERT(bodies[i].type == BODY_JET, "spawned body should be JET type");
        ASSERT(fabs(bodies[i].vz) > fabs(bodies[i].vx), "jet vz should dominate vx");
        ASSERT(fabs(bodies[i].vz) > fabs(bodies[i].vy), "jet vz should dominate vy");
    }

    return 1;
}
```

Register all in `main`:

```c
    // Quasar physics tests
    RUN_TEST(test_accretion_mass_conservation);
    RUN_TEST(test_feedback_pushes_outward);
    RUN_TEST(test_eddington_cap);
    RUN_TEST(test_jet_spawn_direction);
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build && ./build/test_physics`
Expected: All 16 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_physics.c
git commit -m "test: add quasar feedback, Eddington cap, and jet direction tests"
```

---

## Task 5: Quasar Initial Conditions

**Files:**
- Modify: `src/initial_conditions.h`
- Modify: `src/initial_conditions.c`

- [ ] **Step 1: Write test for quasar galaxy generation**

Add to `tests/test_physics.c` before `main`:

```c
static int test_quasar_galaxy_generation(void)
{
    int n = 500;
    Body *bodies = calloc(n, sizeof(Body));
    generate_quasar_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0, 0.0, 0.0, 0.05);

    // Body 0 should be SMBH
    ASSERT(bodies[0].type == BODY_SMBH, "first body should be SMBH");
    ASSERT_NEAR(bodies[0].spin_z, 1.0, 1e-12, "SMBH spin should be +z");
    ASSERT(bodies[0].mass > 0, "SMBH should have positive mass");

    // Should have some GAS bodies in inner region
    int gas_count = 0;
    for (int i = 1; i < n; i++) {
        if (bodies[i].type == BODY_GAS) gas_count++;
        ASSERT(bodies[i].mass > 0, "all bodies should have positive mass");
    }
    ASSERT(gas_count > 0, "should have pre-seeded gas bodies");

    free(bodies);
    return 1;
}
```

Register in `main`:

```c
    RUN_TEST(test_quasar_galaxy_generation);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/test_physics`
Expected: FAIL — `generate_quasar_galaxy` not declared.

- [ ] **Step 3: Add declarations to initial_conditions.h**

Replace `src/initial_conditions.h`:

```c
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
```

- [ ] **Step 4: Implement quasar initial conditions**

Add to the end of `src/initial_conditions.c`:

```c
void generate_quasar_galaxy(Body *bodies, int n, double cx, double cy,
                            double galaxy_mass, double disk_radius,
                            double vx_bulk, double vy_bulk,
                            double smbh_mass_frac)
{
    // Generate base galaxy
    generate_spiral_galaxy(bodies, n, cx, cy, galaxy_mass, disk_radius, vx_bulk, vy_bulk);

    // Upgrade central body to SMBH
    bodies[0].mass = galaxy_mass * smbh_mass_frac;
    bodies[0].type = BODY_SMBH;
    bodies[0].spin_x = 0.0;
    bodies[0].spin_y = 0.0;
    bodies[0].spin_z = 1.0;
    bodies[0].accretion_rate = 0.0;
    bodies[0].luminosity = 0.0;

    // Pre-seed inner 20% as gas
    double inner_radius_sq = (disk_radius * 0.2) * (disk_radius * 0.2);
    for (int i = 1; i < n; i++) {
        double dx = bodies[i].x - cx;
        double dy = bodies[i].y - cy;
        if (dx * dx + dy * dy < inner_radius_sq) {
            bodies[i].type = BODY_GAS;
        }
    }
}

void generate_quasar_merger(Body *bodies, int n, double separation,
                            double approach_vel, double smbh_mass_frac)
{
    int n1 = n / 2;
    int n2 = n - n1;

    double disk_radius = separation * 0.15;
    double galaxy_mass = (double)n1 * 2.0;

    // Galaxy 1: left, moving right
    generate_quasar_galaxy(bodies, n1,
                           -separation * 0.5, 0.0,
                           galaxy_mass, disk_radius,
                           approach_vel, approach_vel * 0.3,
                           smbh_mass_frac);

    // Galaxy 2: right, moving left (re-seed RNG for different structure)
    rng_seed((uint64_t)time(NULL) + 12345);
    generate_quasar_galaxy(bodies + n1, n2,
                           separation * 0.5, 0.0,
                           galaxy_mass * 0.7, disk_radius * 0.8,
                           -approach_vel, -approach_vel * 0.3,
                           smbh_mass_frac);
}
```

Note: `rng_seed` is `static` in `initial_conditions.c`, so `generate_quasar_merger` can call it directly.

- [ ] **Step 5: Build and run tests**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: All 17 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/initial_conditions.c src/initial_conditions.h tests/test_physics.c
git commit -m "feat: add quasar galaxy and merger initial condition generators"
```

---

## Task 6: CLI Flags and Main Loop Integration

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Add quasar CLI flags and config to main**

In `src/main.c`, add the quasar include after the other includes (line 14):

```c
#include "quasar.h"
```

Add new defaults after the existing `#define` block (after line 22):

```c
#define DEFAULT_SMBH_MASS_FRAC 0.05
#define DEFAULT_ACCRETION_RADIUS 3.0
#define DEFAULT_JET_SPEED 20.0
#define DEFAULT_FEEDBACK_STRENGTH 1.0
```

- [ ] **Step 2: Update argument parsing in main()**

Replace the variable declarations at the start of `main` (line 133-136) and the arg parsing loop (line 138-162):

```c
int main(int argc, char **argv)
{
    int n = DEFAULT_N;
    int merger = 0;
    int quasar = 0;
    int high_fidelity = 0;
    double dt = DT;
    double theta = THETA;
    double smbh_mass_frac = DEFAULT_SMBH_MASS_FRAC;
    double accretion_radius = DEFAULT_ACCRETION_RADIUS;
    double jet_speed = DEFAULT_JET_SPEED;
    double feedback_strength = DEFAULT_FEEDBACK_STRENGTH;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--merger") == 0) {
            merger = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quasar") == 0) {
            quasar = 1;
        } else if (strcmp(argv[i], "--high-fidelity") == 0) {
            high_fidelity = 1;
        } else if (strcmp(argv[i], "-dt") == 0 && i + 1 < argc) {
            dt = atof(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            theta = atof(argv[++i]);
        } else if (strcmp(argv[i], "--smbh-mass") == 0 && i + 1 < argc) {
            smbh_mass_frac = atof(argv[++i]);
        } else if (strcmp(argv[i], "--accretion-radius") == 0 && i + 1 < argc) {
            accretion_radius = atof(argv[++i]);
        } else if (strcmp(argv[i], "--jet-speed") == 0 && i + 1 < argc) {
            jet_speed = atof(argv[++i]);
        } else if (strcmp(argv[i], "--feedback-strength") == 0 && i + 1 < argc) {
            feedback_strength = atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: cosmosim [options]\n"
                   "  -n <count>              Number of bodies (default %d)\n"
                   "  -m, --merger            Galaxy merger mode\n"
                   "  -q, --quasar            Enable quasar physics (SMBH + accretion + jets)\n"
                   "  --high-fidelity         Higher substeps and jet density\n"
                   "  -dt <value>             Timestep (default %.4f)\n"
                   "  -t <theta>              Barnes-Hut opening angle (default %.1f)\n"
                   "  --smbh-mass <frac>      SMBH mass fraction (default %.2f)\n"
                   "  --accretion-radius <r>  Accretion radius (default %.1f)\n"
                   "  --jet-speed <v>         Jet particle speed (default %.1f)\n"
                   "  --feedback-strength <s> Feedback multiplier (default %.1f)\n"
                   "\nControls:\n"
                   "  Scroll        Zoom in/out\n"
                   "  Left-drag     Orbit camera\n"
                   "  Right-drag    Pan\n"
                   "  Space         Pause/resume\n"
                   "  R             Reset camera\n"
                   "  Q/Esc         Quit\n",
                   DEFAULT_N, DT, THETA,
                   DEFAULT_SMBH_MASS_FRAC, DEFAULT_ACCRETION_RADIUS,
                   DEFAULT_JET_SPEED, DEFAULT_FEEDBACK_STRENGTH);
            return 0;
        }
    }

    if (n < 2) n = 2;
    int substeps = high_fidelity ? 8 : SUBSTEPS;
```

- [ ] **Step 3: Update body allocation with headroom for jets**

Replace the allocation section (lines 209-216):

```c
    // Allocate bodies with headroom for jet particles
    int n_alloc = quasar ? n + n / 4 : n;
    Body *bodies = calloc(n_alloc, sizeof(Body));
    OctreeNode *pool = malloc(8 * n_alloc * sizeof(OctreeNode));

    if (!bodies || !pool) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
```

- [ ] **Step 4: Update initial condition generation**

Replace the initial condition block (lines 218-223):

```c
    // Generate initial conditions
    if (quasar) {
        if (merger) {
            generate_quasar_merger(bodies, n, 60.0, 0.3, smbh_mass_frac);
        } else {
            generate_quasar_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0,
                                   0.0, 0.0, smbh_mass_frac);
        }
    } else {
        if (merger) {
            generate_merger(bodies, n, 60.0, 0.3);
        } else {
            generate_spiral_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0, 0.0, 0.0);
        }
    }
```

- [ ] **Step 5: Set up quasar config and update simulation loop**

After `integrator_init_accelerations` (line 226), add quasar config setup:

```c
    QuasarConfig qcfg = quasar_default_config();
    if (quasar) {
        qcfg.accretion_radius = accretion_radius;
        qcfg.jet_speed = jet_speed;
        qcfg.feedback_strength = feedback_strength;
        qcfg.jet_cap = high_fidelity ? n / 4 : n / 10;
        qcfg.max_bodies = n_alloc;
    }
    int current_n = n;
    int compact_counter = 0;
    int compact_interval = high_fidelity ? 60 : 120;
```

Update the print line:

```c
    printf("cosmosim: %d bodies, %s%s mode, dt=%.4f, theta=%.2f\n",
           n, merger ? "merger" : "galaxy", quasar ? " quasar" : "", dt, theta);
```

Replace the simulation loop body (lines 237-241):

```c
        if (!paused) {
            for (int sub = 0; sub < substeps; sub++) {
                integrator_step(bodies, current_n, dt, G, SOFTENING, theta, pool);
                if (quasar) {
                    quasar_step(bodies, &current_n, n_alloc, &qcfg, dt);
                }
            }
            if (quasar) {
                compact_counter++;
                if (compact_counter >= compact_interval) {
                    current_n = quasar_compact(bodies, current_n);
                    compact_counter = 0;
                }
            }
        }
```

Update `renderer_draw` call to use `current_n`:

```c
        renderer_draw(bodies, current_n, &camera, w, h);
```

Update the FPS title to show `current_n`:

```c
            snprintf(title, sizeof(title), "cosmosim | %d bodies | %.0f FPS%s",
                     current_n, fps_frames / (now - fps_time), paused ? " [PAUSED]" : "");
```

- [ ] **Step 6: Build and test**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: All tests pass.

Then run the simulation in quasar mode to sanity check:

Run: `./build/cosmosim -q -n 10000`
Expected: Window opens, SMBH at center, jets should begin spawning. Bodies near center get accreted.

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "feat: integrate quasar physics into main loop with CLI flags"
```

---

## Task 7: HDR Framebuffer and Type-Aware Shaders

**Files:**
- Modify: `src/renderer.h`
- Modify: `src/renderer.c`
- Modify: `src/shaders/particle.vert`
- Modify: `src/shaders/particle.frag`
- Create: `src/shaders/fullscreen.vert`

- [ ] **Step 1: Add RendererConfig to renderer.h**

Replace `src/renderer.h`:

```c
#ifndef RENDERER_H
#define RENDERER_H

#include "body.h"

typedef struct {
    float azimuth;
    float elevation;
    float distance;
    float target_x, target_y, target_z;
} Camera;

typedef struct {
    int hdr_enabled;           // 1 if quasar mode
    float smbh_x, smbh_y, smbh_z;  // screen-space updated each frame
    float smbh_luminosity;
    float smbh_mass;
    int bloom_iterations;      // 2 default, 4 high-fidelity
    int lensing_samples;       // 1 default, 4 high-fidelity
} RendererConfig;

int renderer_init(const RendererConfig *rcfg);
void renderer_update_smbh(RendererConfig *rcfg, const Body *bodies, int n);
void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height,
                   const RendererConfig *rcfg);
void renderer_cleanup(void);

#endif
```

- [ ] **Step 2: Create fullscreen quad vertex shader**

Create `src/shaders/fullscreen.vert`:

```glsl
#version 330 core

out vec2 v_uv;

void main()
{
    // Full-screen triangle trick: 3 vertices, no VBO needed
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
```

- [ ] **Step 3: Update particle vertex shader with velocity and type**

Replace `src/shaders/particle.vert`:

```glsl
#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in float a_mass;
layout(location = 2) in vec3 a_velocity;
layout(location = 3) in float a_type;

uniform mat4 u_view;
uniform mat4 u_projection;

out float v_mass;
out float v_type;
out vec3 v_velocity;
out float v_view_depth;

void main()
{
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position = u_projection * view_pos;
    v_mass = a_mass;
    v_type = a_type;
    v_velocity = a_velocity;
    v_view_depth = -view_pos.z;

    float base_size;
    int body_type = int(a_type + 0.5);

    if (body_type == 2) {
        // SMBH: large bright point
        base_size = 30.0;
    } else if (body_type == 3) {
        // JET: elongated, medium size
        base_size = clamp(3.0 + length(a_velocity) * 0.1, 3.0, 15.0);
    } else {
        // STAR/GAS: same as before
        base_size = clamp(1.0 + log(a_mass + 1.0) * 2.0, 1.0, 20.0);
    }

    gl_PointSize = base_size * 300.0 / (-view_pos.z);
    gl_PointSize = clamp(gl_PointSize, 1.0, 60.0);
}
```

- [ ] **Step 4: Update particle fragment shader with type-aware coloring**

Replace `src/shaders/particle.frag`:

```glsl
#version 330 core
in float v_mass;
in float v_type;
in vec3 v_velocity;
in float v_view_depth;

uniform vec3 u_smbh_pos;
uniform float u_smbh_luminosity;

out vec4 frag_color;

void main()
{
    vec2 coord = gl_PointCoord - vec2(0.5);
    float r_sq = dot(coord, coord);

    if (r_sq > 0.25) discard;

    float intensity = exp(-r_sq * 16.0);
    int body_type = int(v_type + 0.5);
    vec3 color;

    if (body_type == 2) {
        // SMBH: bright white core
        color = vec3(2.0, 2.0, 2.5);  // HDR >1 for bloom
        intensity = exp(-r_sq * 8.0);  // wider glow
    } else if (body_type == 3) {
        // JET: blue-white with cyan edges
        float core = exp(-r_sq * 24.0);
        vec3 core_color = vec3(1.5, 1.8, 2.5);  // HDR blue-white
        vec3 edge_color = vec3(0.3, 0.8, 1.2);
        color = mix(edge_color, core_color, core);
    } else if (body_type == 1) {
        // GAS: temperature-based (distance to SMBH approximated by luminosity uniform)
        // Hot gas near center: white. Cool gas far: red-orange
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        vec3 cool_gas = vec3(1.0, 0.4, 0.1);    // red-orange
        vec3 hot_gas = vec3(1.5, 1.3, 1.2);      // bright white-yellow (HDR)
        // Boost brightness by SMBH luminosity proximity (rough approx)
        float brightness_boost = u_smbh_luminosity * 0.01;
        color = mix(cool_gas, hot_gas, t) * (1.0 + brightness_boost);
    } else {
        // STAR: original blue-to-orange gradient
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        color = mix(vec3(0.6, 0.7, 1.0), vec3(1.0, 0.8, 0.3), t);
    }

    frag_color = vec4(color * intensity, intensity * 0.8);
}
```

- [ ] **Step 5: Update renderer.c — HDR FBO setup and type-aware upload**

Replace the entire `src/renderer.c`:

```c
#include "renderer.h"
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef SHADER_DIR
#define SHADER_DIR "src/shaders/"
#endif

// Particle rendering
static GLuint particle_program;
static GLuint vao, vbo;
static GLint u_view_loc, u_proj_loc;
static GLint u_smbh_pos_loc, u_smbh_lum_loc;
static float *upload_buf = NULL;
static int upload_buf_capacity = 0;

// HDR framebuffer
static GLuint hdr_fbo, hdr_color_tex, hdr_depth_rbo;
static int hdr_width = 0, hdr_height = 0;
static int hdr_active = 0;

// Fullscreen quad VAO (no VBO — uses gl_VertexID trick)
static GLuint fullscreen_vao;

// Post-process programs (initialized in later tasks, stubs for now)
static GLuint bloom_extract_program = 0;
static GLuint bloom_blur_program = 0;
static GLuint lensing_program = 0;
static GLuint composite_program = 0;

// Bloom FBOs
static GLuint bloom_fbo[2], bloom_tex[2];
static int bloom_width = 0, bloom_height = 0;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open shader: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t rd = fread(buf, 1, len, f);
    (void)rd;
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile_shader(const char *path, GLenum type)
{
    char *src = read_file(path);
    if (!src) return 0;

    GLuint s = glCreateShader(type);
    const char *src_ptr = src;
    glShaderSource(s, 1, &src_ptr, NULL);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error (%s):\n%s\n", path, log);
        glDeleteShader(s);
        free(src);
        return 0;
    }
    free(src);
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "Shader link error:\n%s\n", log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint load_program(const char *vert_name, const char *frag_name)
{
    char vp[512], fp[512];
    snprintf(vp, sizeof(vp), "%s%s", SHADER_DIR, vert_name);
    snprintf(fp, sizeof(fp), "%s%s", SHADER_DIR, frag_name);
    GLuint vs = compile_shader(vp, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fp, GL_FRAGMENT_SHADER);
    if (!vs || !fs) return 0;
    return link_program(vs, fs);
}

static void setup_hdr_fbo(int width, int height)
{
    if (hdr_fbo) {
        glDeleteFramebuffers(1, &hdr_fbo);
        glDeleteTextures(1, &hdr_color_tex);
        glDeleteRenderbuffers(1, &hdr_depth_rbo);
    }

    glGenFramebuffers(1, &hdr_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo);

    glGenTextures(1, &hdr_color_tex);
    glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdr_color_tex, 0);

    glGenRenderbuffers(1, &hdr_depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, hdr_depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdr_depth_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    hdr_width = width;
    hdr_height = height;
}

static void setup_bloom_fbos(int width, int height)
{
    int bw = width / 2;
    int bh = height / 2;

    if (bloom_fbo[0]) {
        glDeleteFramebuffers(2, bloom_fbo);
        glDeleteTextures(2, bloom_tex);
    }

    for (int i = 0; i < 2; i++) {
        glGenFramebuffers(1, &bloom_fbo[i]);
        glGenTextures(1, &bloom_tex[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[i]);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom_tex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bloom_width = bw;
    bloom_height = bh;
}

int renderer_init(const RendererConfig *rcfg)
{
    hdr_active = rcfg ? rcfg->hdr_enabled : 0;

    // Particle shader
    particle_program = load_program("particle.vert", "particle.frag");
    if (!particle_program) return -1;

    u_view_loc = glGetUniformLocation(particle_program, "u_view");
    u_proj_loc = glGetUniformLocation(particle_program, "u_projection");
    u_smbh_pos_loc = glGetUniformLocation(particle_program, "u_smbh_pos");
    u_smbh_lum_loc = glGetUniformLocation(particle_program, "u_smbh_luminosity");

    // VAO/VBO with expanded layout: pos(3) + mass(1) + vel(3) + type(1) = 8 floats
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    int stride = 8 * sizeof(float);
    // position (vec3)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);
    // mass (float)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // velocity (vec3)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // type (float)
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void *)(7 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    // Fullscreen quad VAO (no attributes — uses gl_VertexID)
    glGenVertexArrays(1, &fullscreen_vao);

    // Post-processing shaders (loaded if HDR enabled)
    if (hdr_active) {
        bloom_extract_program = load_program("fullscreen.vert", "bloom_extract.frag");
        bloom_blur_program = load_program("fullscreen.vert", "bloom_blur.frag");
        lensing_program = load_program("fullscreen.vert", "lensing.frag");
        composite_program = load_program("fullscreen.vert", "composite.frag");

        if (!bloom_extract_program || !bloom_blur_program ||
            !lensing_program || !composite_program) {
            fprintf(stderr, "Warning: some post-processing shaders failed to load\n");
        }
    }

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 0;
}

void renderer_update_smbh(RendererConfig *rcfg, const Body *bodies, int n)
{
    for (int i = 0; i < n; i++) {
        if (bodies[i].type == BODY_SMBH && bodies[i].mass > 0.0) {
            rcfg->smbh_x = (float)bodies[i].x;
            rcfg->smbh_y = (float)bodies[i].y;
            rcfg->smbh_z = (float)bodies[i].z;
            rcfg->smbh_luminosity = (float)bodies[i].luminosity;
            rcfg->smbh_mass = (float)bodies[i].mass;
            break; // use first SMBH found
        }
    }
}

static void build_perspective(float *m, float fov_rad, float aspect, float near, float far)
{
    float f = 1.0f / tanf(fov_rad * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void build_look_at(float *m, float ex, float ey, float ez,
                           float tx, float ty, float tz)
{
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (fl < 1e-12f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    float rx = fy * 1.0f - fz * 0.0f;
    float ry = fz * 0.0f - fx * 1.0f;
    float rz = fx * 0.0f - fy * 0.0f;
    float rl = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rl < 1e-12f) {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    } else {
        rx /= rl; ry /= rl; rz /= rl;
    }

    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    memset(m, 0, 16 * sizeof(float));
    m[0] = rx;  m[4] = ry;  m[8]  = rz;  m[12] = -(rx*ex + ry*ey + rz*ez);
    m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux*ex + uy*ey + uz*ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx*ex + fy*ey + fz*ez);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1.0f;
}

static void draw_fullscreen_quad(void)
{
    glBindVertexArray(fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height,
                   const RendererConfig *rcfg)
{
    // Ensure upload buffer capacity (8 floats per body)
    if (n * 8 > upload_buf_capacity) {
        upload_buf_capacity = n * 8;
        upload_buf = realloc(upload_buf, upload_buf_capacity * sizeof(float));
    }

    // Upload: x, y, z, mass, vx, vy, vz, type
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        int off = count * 8;
        upload_buf[off + 0] = (float)bodies[i].x;
        upload_buf[off + 1] = (float)bodies[i].y;
        upload_buf[off + 2] = (float)bodies[i].z;
        upload_buf[off + 3] = (float)bodies[i].mass;
        upload_buf[off + 4] = (float)bodies[i].vx;
        upload_buf[off + 5] = (float)bodies[i].vy;
        upload_buf[off + 6] = (float)bodies[i].vz;
        upload_buf[off + 7] = (float)bodies[i].type;
        count++;
    }

    // Setup HDR FBO if needed
    if (hdr_active && rcfg) {
        if (window_width != hdr_width || window_height != hdr_height) {
            setup_hdr_fbo(window_width, window_height);
            setup_bloom_fbos(window_width, window_height);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo);
        glViewport(0, 0, window_width, window_height);
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // Camera
    float cos_el = cosf(cam->elevation);
    float eye_x = cam->target_x + cam->distance * cos_el * cosf(cam->azimuth);
    float eye_y = cam->target_y + cam->distance * cos_el * sinf(cam->azimuth);
    float eye_z = cam->target_z + cam->distance * sinf(cam->elevation);

    float view[16], proj[16];
    build_look_at(view, eye_x, eye_y, eye_z,
                  cam->target_x, cam->target_y, cam->target_z);

    float aspect = (float)window_width / (float)window_height;
    build_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, cam->distance * 10.0f);

    // Draw particles
    glUseProgram(particle_program);
    glUniformMatrix4fv(u_view_loc, 1, GL_FALSE, view);
    glUniformMatrix4fv(u_proj_loc, 1, GL_FALSE, proj);

    if (rcfg && u_smbh_pos_loc >= 0) {
        glUniform3f(u_smbh_pos_loc, rcfg->smbh_x, rcfg->smbh_y, rcfg->smbh_z);
    }
    if (rcfg && u_smbh_lum_loc >= 0) {
        glUniform1f(u_smbh_lum_loc, rcfg->smbh_luminosity);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * 8 * sizeof(float), upload_buf, GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);

    // Post-processing (HDR path)
    if (hdr_active && rcfg && bloom_extract_program && composite_program) {
        int iterations = rcfg->bloom_iterations > 0 ? rcfg->bloom_iterations : 2;

        // Pass 2: Bloom extract
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
        glViewport(0, 0, bloom_width, bloom_height);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(bloom_extract_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
        glUniform1i(glGetUniformLocation(bloom_extract_program, "u_scene"), 0);
        glUniform1f(glGetUniformLocation(bloom_extract_program, "u_threshold"), 1.0f);
        draw_fullscreen_quad();

        // Pass 2b: Gaussian blur ping-pong
        if (bloom_blur_program) {
            for (int i = 0; i < iterations; i++) {
                // Horizontal
                glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[1]);
                glClear(GL_COLOR_BUFFER_BIT);
                glUseProgram(bloom_blur_program);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
                glUniform1i(glGetUniformLocation(bloom_blur_program, "u_image"), 0);
                glUniform2f(glGetUniformLocation(bloom_blur_program, "u_direction"),
                            1.0f / bloom_width, 0.0f);
                draw_fullscreen_quad();

                // Vertical
                glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
                glClear(GL_COLOR_BUFFER_BIT);
                glBindTexture(GL_TEXTURE_2D, bloom_tex[1]);
                glUniform2f(glGetUniformLocation(bloom_blur_program, "u_direction"),
                            0.0f, 1.0f / bloom_height);
                draw_fullscreen_quad();
            }
        }

        // Pass 3: Gravitational lensing (applied to HDR scene)
        if (lensing_program) {
            // Compute SMBH screen position
            float smbh_clip[4];
            float mv[4] = {
                view[0]*rcfg->smbh_x + view[4]*rcfg->smbh_y + view[8]*rcfg->smbh_z + view[12],
                view[1]*rcfg->smbh_x + view[5]*rcfg->smbh_y + view[9]*rcfg->smbh_z + view[13],
                view[2]*rcfg->smbh_x + view[6]*rcfg->smbh_y + view[10]*rcfg->smbh_z + view[14],
                view[3]*rcfg->smbh_x + view[7]*rcfg->smbh_y + view[11]*rcfg->smbh_z + view[15]
            };
            smbh_clip[0] = proj[0]*mv[0] + proj[4]*mv[1] + proj[8]*mv[2] + proj[12]*mv[3];
            smbh_clip[1] = proj[1]*mv[0] + proj[5]*mv[1] + proj[9]*mv[2] + proj[13]*mv[3];
            smbh_clip[3] = proj[3]*mv[0] + proj[7]*mv[1] + proj[11]*mv[2] + proj[15]*mv[3];

            float smbh_ndc_x = 0.5f, smbh_ndc_y = 0.5f;
            if (smbh_clip[3] > 0.001f) {
                smbh_ndc_x = (smbh_clip[0] / smbh_clip[3]) * 0.5f + 0.5f;
                smbh_ndc_y = (smbh_clip[1] / smbh_clip[3]) * 0.5f + 0.5f;
            }

            // Render lensed scene to a temp — reuse bloom_fbo[1] at full res
            // Actually, apply lensing in the composite pass to avoid extra FBO
            // Store screen pos for composite
            glUseProgram(composite_program);
            glUniform2f(glGetUniformLocation(composite_program, "u_smbh_screen"),
                        smbh_ndc_x, smbh_ndc_y);
            glUniform1f(glGetUniformLocation(composite_program, "u_smbh_mass"),
                        rcfg->smbh_mass);
            glUniform1f(glGetUniformLocation(composite_program, "u_lensing_strength"),
                        rcfg->smbh_mass * 0.0001f);
        }

        // Pass 4: Composite to screen
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width, window_height);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(composite_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
        glUniform1i(glGetUniformLocation(composite_program, "u_scene"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
        glUniform1i(glGetUniformLocation(composite_program, "u_bloom"), 1);
        glUniform1f(glGetUniformLocation(composite_program, "u_bloom_intensity"), 0.5f);
        draw_fullscreen_quad();
    } else if (hdr_active) {
        // Fallback: just blit HDR to screen without post-processing
        glBindFramebuffer(GL_READ_FRAMEBUFFER, hdr_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, hdr_width, hdr_height,
                          0, 0, window_width, window_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void renderer_cleanup(void)
{
    glDeleteProgram(particle_program);
    if (bloom_extract_program) glDeleteProgram(bloom_extract_program);
    if (bloom_blur_program) glDeleteProgram(bloom_blur_program);
    if (lensing_program) glDeleteProgram(lensing_program);
    if (composite_program) glDeleteProgram(composite_program);

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &fullscreen_vao);

    if (hdr_fbo) {
        glDeleteFramebuffers(1, &hdr_fbo);
        glDeleteTextures(1, &hdr_color_tex);
        glDeleteRenderbuffers(1, &hdr_depth_rbo);
    }
    if (bloom_fbo[0]) {
        glDeleteFramebuffers(2, bloom_fbo);
        glDeleteTextures(2, bloom_tex);
    }

    free(upload_buf);
}
```

- [ ] **Step 6: Update main.c renderer calls to pass RendererConfig**

In `src/main.c`, before `renderer_init()` call, create the config:

```c
    RendererConfig rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.hdr_enabled = quasar;
    rcfg.bloom_iterations = high_fidelity ? 4 : 2;
    rcfg.lensing_samples = high_fidelity ? 4 : 1;

    if (renderer_init(&rcfg) != 0) {
```

Update the `renderer_draw` call in the loop:

```c
        if (quasar) {
            renderer_update_smbh(&rcfg, bodies, current_n);
        }
        renderer_draw(bodies, current_n, &camera, w, h, &rcfg);
```

- [ ] **Step 7: Build and run**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: All tests pass (physics tests don't use renderer).

Run: `./build/cosmosim -n 5000`
Expected: Non-quasar mode still works (HDR disabled). Shaders now have velocity/type but stars default to type 0.

- [ ] **Step 8: Commit**

```bash
git add src/renderer.h src/renderer.c src/shaders/particle.vert src/shaders/particle.frag src/shaders/fullscreen.vert src/main.c
git commit -m "feat: HDR framebuffer pipeline with type-aware particle shaders"
```

---

## Task 8: Post-Processing Shaders — Bloom, Lensing, Composite

**Files:**
- Create: `src/shaders/bloom_extract.frag`
- Create: `src/shaders/bloom_blur.frag`
- Create: `src/shaders/lensing.frag`
- Create: `src/shaders/composite.frag`

- [ ] **Step 1: Create bloom extraction shader**

Create `src/shaders/bloom_extract.frag`:

```glsl
#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_threshold;

out vec4 frag_color;

void main()
{
    vec3 color = texture(u_scene, v_uv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    if (brightness > u_threshold) {
        frag_color = vec4(color - vec3(u_threshold), 1.0);
    } else {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
```

- [ ] **Step 2: Create Gaussian blur shader**

Create `src/shaders/bloom_blur.frag`:

```glsl
#version 330 core

in vec2 v_uv;
uniform sampler2D u_image;
uniform vec2 u_direction; // (1/w, 0) for horizontal, (0, 1/h) for vertical

out vec4 frag_color;

void main()
{
    // 9-tap Gaussian kernel
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec3 result = texture(u_image, v_uv).rgb * weights[0];

    for (int i = 1; i < 5; i++) {
        vec2 offset = u_direction * float(i);
        result += texture(u_image, v_uv + offset).rgb * weights[i];
        result += texture(u_image, v_uv - offset).rgb * weights[i];
    }

    frag_color = vec4(result, 1.0);
}
```

- [ ] **Step 3: Create gravitational lensing shader**

Create `src/shaders/lensing.frag`:

```glsl
#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform vec2 u_smbh_screen;     // SMBH position in UV coords [0,1]
uniform float u_lensing_strength; // strength factor

out vec4 frag_color;

void main()
{
    vec2 delta = v_uv - u_smbh_screen;
    float dist_sq = dot(delta, delta);
    float dist = sqrt(dist_sq);

    // Radial displacement: stronger close to SMBH
    vec2 displaced_uv = v_uv;
    if (dist > 0.001) {
        float deflection = u_lensing_strength / (dist_sq + 0.001);
        deflection = min(deflection, 0.1); // cap to prevent extreme warping
        displaced_uv = v_uv + normalize(delta) * deflection;
    }

    // Clamp to valid UV range
    displaced_uv = clamp(displaced_uv, vec2(0.0), vec2(1.0));

    frag_color = texture(u_scene, displaced_uv);
}
```

- [ ] **Step 4: Create composite + tonemap shader**

Create `src/shaders/composite.frag`:

```glsl
#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_intensity;
uniform vec2 u_smbh_screen;
uniform float u_smbh_mass;
uniform float u_lensing_strength;

out vec4 frag_color;

void main()
{
    // Apply gravitational lensing distortion to scene UV
    vec2 delta = v_uv - u_smbh_screen;
    float dist_sq = dot(delta, delta);
    float dist = sqrt(dist_sq);

    vec2 scene_uv = v_uv;
    if (dist > 0.001 && u_lensing_strength > 0.0) {
        float deflection = u_lensing_strength / (dist_sq + 0.001);
        deflection = min(deflection, 0.1);
        scene_uv = v_uv + normalize(delta) * deflection;
        scene_uv = clamp(scene_uv, vec2(0.0), vec2(1.0));
    }

    vec3 hdr_color = texture(u_scene, scene_uv).rgb;
    vec3 bloom_color = texture(u_bloom, v_uv).rgb;

    // Combine scene + bloom
    vec3 combined = hdr_color + bloom_color * u_bloom_intensity;

    // Reinhard tonemap
    vec3 ldr = combined / (vec3(1.0) + combined);

    // Gamma correction
    ldr = pow(ldr, vec3(1.0 / 2.2));

    frag_color = vec4(ldr, 1.0);
}
```

- [ ] **Step 5: Build and test visually**

Run: `cmake --build build && ./build/cosmosim -q -n 15000`
Expected: Window opens with quasar mode. You should see:
- Bright white SMBH at center with bloom glow
- Red-orange gas particles near center
- Blue-white jet streaks along z-axis
- Subtle lensing distortion near the SMBH
- Stars in the outer disk look normal

- [ ] **Step 6: Test non-quasar mode still works**

Run: `./build/cosmosim -n 10000`
Expected: Normal galaxy view, no HDR artifacts. Looks identical to before (HDR disabled).

- [ ] **Step 7: Test merger quasar mode**

Run: `./build/cosmosim -q -m -n 20000`
Expected: Two galaxies approaching, each with an SMBH. Jets from both SMBHs once accretion begins.

- [ ] **Step 8: Commit**

```bash
git add src/shaders/bloom_extract.frag src/shaders/bloom_blur.frag src/shaders/lensing.frag src/shaders/composite.frag
git commit -m "feat: add bloom, lensing, and composite post-processing shaders"
```

---

## Task 9: Final Integration Test and Polish

**Files:**
- Modify: `tests/test_physics.c`

- [ ] **Step 1: Add a compact test**

Add to `tests/test_physics.c` before `main`:

```c
static int test_compact_removes_dead(void)
{
    Body bodies[5];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].mass = 1.0; bodies[0].type = BODY_STAR;
    bodies[1].mass = 0.0; // dead
    bodies[2].mass = 3.0; bodies[2].type = BODY_GAS;
    bodies[3].mass = 0.0; // dead
    bodies[4].mass = 5.0; bodies[4].type = BODY_STAR;

    int new_n = quasar_compact(bodies, 5);
    ASSERT(new_n == 3, "compact should remove 2 dead bodies");
    ASSERT_NEAR(bodies[0].mass, 1.0, 1e-12, "body 0 mass preserved");
    ASSERT_NEAR(bodies[1].mass, 3.0, 1e-12, "body 1 should be former body 2");
    ASSERT_NEAR(bodies[2].mass, 5.0, 1e-12, "body 2 should be former body 4");

    return 1;
}
```

Register in `main`:

```c
    RUN_TEST(test_compact_removes_dead);
```

- [ ] **Step 2: Run full test suite**

Run: `cmake --build build && ./build/test_physics`
Expected: All 18 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_physics.c
git commit -m "test: add dead body compaction test"
```

- [ ] **Step 4: Run full build from clean state**

Run: `rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics`
Expected: Clean build, all tests pass.

- [ ] **Step 5: Final visual validation**

Run each of these commands and verify visually:

```bash
./build/cosmosim -n 10000                    # non-quasar: unchanged behavior
./build/cosmosim -q -n 15000                 # single galaxy quasar
./build/cosmosim -q -m -n 20000             # merger quasar
./build/cosmosim -q -n 30000 --high-fidelity # high-fidelity mode
```

- [ ] **Step 6: Commit any polish fixes**

If any visual or behavioral issues were found and fixed, commit them:

```bash
git add -u
git commit -m "fix: polish quasar rendering and physics parameters"
```
