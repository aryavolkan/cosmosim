#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "body.h"
#include "octree.h"
#include "integrator.h"
#include "initial_conditions.h"
#include "quasar.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) \
    ASSERT(fabs((a) - (b)) < (tol), msg)

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("Running %s...\n", #fn); \
    if (fn()) { tests_passed++; printf("  PASS\n"); } \
} while(0)

/* ---- helpers ---- */

static double compute_total_energy(const Body *bodies, int n, double G, double softening)
{
    double ke = 0.0;
    for (int i = 0; i < n; i++) {
        ke += 0.5 * bodies[i].mass * (bodies[i].vx * bodies[i].vx +
              bodies[i].vy * bodies[i].vy + bodies[i].vz * bodies[i].vz);
    }
    double pe = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = bodies[j].x - bodies[i].x;
            double dy = bodies[j].y - bodies[i].y;
            double dz = bodies[j].z - bodies[i].z;
            double r = sqrt(dx*dx + dy*dy + dz*dz + softening*softening);
            pe -= G * bodies[i].mass * bodies[j].mass / r;
        }
    }
    return ke + pe;
}

static void compute_direct_forces(Body *bodies, int n, double G, double softening)
{
    double soft_sq = softening * softening;
    for (int i = 0; i < n; i++) {
        bodies[i].ax = bodies[i].ay = bodies[i].az = 0.0;
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            double dx = bodies[j].x - bodies[i].x;
            double dy = bodies[j].y - bodies[i].y;
            double dz = bodies[j].z - bodies[i].z;
            double r_sq = dx*dx + dy*dy + dz*dz + soft_sq;
            double inv_r3 = 1.0 / (r_sq * sqrt(r_sq));
            bodies[i].ax += G * bodies[j].mass * dx * inv_r3;
            bodies[i].ay += G * bodies[j].mass * dy * inv_r3;
            bodies[i].az += G * bodies[j].mass * dz * inv_r3;
        }
    }
}

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

/* ---- octree tests ---- */

static int test_octree_single_body(void)
{
    Body bodies[1];
    bodies[0] = make_body(3.0, 4.0, 5.0, 0, 0, 0, 7.0);

    OctreeNode *pool = malloc(8 * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, 1);

    ASSERT(pool_size >= 1, "pool should have at least one node");
    ASSERT_NEAR(pool[0].total_mass, 7.0, 1e-12, "root mass should equal body mass");
    ASSERT_NEAR(pool[0].cx, 3.0, 1e-12, "root COM x");
    ASSERT_NEAR(pool[0].cy, 4.0, 1e-12, "root COM y");
    ASSERT_NEAR(pool[0].cz, 5.0, 1e-12, "root COM z");

    free(pool);
    return 1;
}

static int test_octree_two_bodies_com(void)
{
    Body bodies[2];
    bodies[0] = make_body(1.0, 0.0, 0.0, 0, 0, 0, 1.0);
    bodies[1] = make_body(-1.0, 0.0, 0.0, 0, 0, 0, 3.0);

    OctreeNode *pool = malloc(16 * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, 2);

    // COM = (1*1 + (-1)*3) / 4 = -0.5
    ASSERT_NEAR(pool[0].total_mass, 4.0, 1e-12, "total mass should be 4");
    ASSERT_NEAR(pool[0].cx, -0.5, 1e-12, "COM x should be -0.5");
    ASSERT_NEAR(pool[0].cy, 0.0, 1e-12, "COM y should be 0");
    ASSERT_NEAR(pool[0].cz, 0.0, 1e-12, "COM z should be 0");

    free(pool);
    return 1;
}

static int test_octree_mass_conservation(void)
{
    int n = 100;
    Body *bodies = calloc(n, sizeof(Body));
    double total = 0.0;
    for (int i = 0; i < n; i++) {
        bodies[i].x = (double)(i * 17 % 97) - 48.0;
        bodies[i].y = (double)(i * 31 % 89) - 44.0;
        bodies[i].z = (double)(i * 13 % 73) - 36.0;
        bodies[i].mass = 1.0 + (i % 5);
        total += bodies[i].mass;
    }

    OctreeNode *pool = malloc(8 * n * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, n);

    ASSERT_NEAR(pool[0].total_mass, total, 1e-8, "root mass should equal sum of all body masses");

    free(pool);
    free(bodies);
    return 1;
}

/* ---- force tests ---- */

static int test_two_body_force_direction(void)
{
    Body bodies[2];
    bodies[0] = make_body(0, 0, 0, 0, 0, 0, 1.0);
    bodies[1] = make_body(5.0, 0, 0, 0, 0, 0, 1.0);

    OctreeNode *pool = malloc(16 * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, 2);
    octree_compute_forces(pool, 0, bodies, 2, 1.0, 0.01, 0.0);

    // Body 0 should accelerate toward body 1 (+x)
    ASSERT(bodies[0].ax > 0, "body 0 should accelerate in +x");
    ASSERT_NEAR(bodies[0].ay, 0.0, 1e-12, "body 0 ay should be ~0");
    ASSERT_NEAR(bodies[0].az, 0.0, 1e-12, "body 0 az should be ~0");

    // Body 1 should accelerate toward body 0 (-x)
    ASSERT(bodies[1].ax < 0, "body 1 should accelerate in -x");
    ASSERT_NEAR(bodies[1].ay, 0.0, 1e-12, "body 1 ay should be ~0");
    ASSERT_NEAR(bodies[1].az, 0.0, 1e-12, "body 1 az should be ~0");

    free(pool);
    return 1;
}

static int test_force_symmetry_newton3(void)
{
    Body bodies[2];
    bodies[0] = make_body(0, 0, 0, 0, 0, 0, 2.0);
    bodies[1] = make_body(3.0, 4.0, 0, 0, 0, 0, 5.0);

    OctreeNode *pool = malloc(16 * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, bodies, 2);
    octree_compute_forces(pool, 0, bodies, 2, 1.0, 0.01, 0.0);

    // F = m*a, Newton's 3rd: m0*a0 = -m1*a1
    double f0x = bodies[0].mass * bodies[0].ax;
    double f0y = bodies[0].mass * bodies[0].ay;
    double f1x = bodies[1].mass * bodies[1].ax;
    double f1y = bodies[1].mass * bodies[1].ay;

    ASSERT_NEAR(f0x, -f1x, 1e-10, "Newton's 3rd law: Fx");
    ASSERT_NEAR(f0y, -f1y, 1e-10, "Newton's 3rd law: Fy");

    free(pool);
    return 1;
}

static int test_force_inverse_square(void)
{
    double softening = 0.0001;

    // Distance d1 = 5
    Body b1[2];
    b1[0] = make_body(0, 0, 0, 0, 0, 0, 1.0);
    b1[1] = make_body(5.0, 0, 0, 0, 0, 0, 1.0);
    OctreeNode *pool1 = malloc(16 * sizeof(OctreeNode));
    int ps1 = 0;
    octree_build(pool1, &ps1, b1, 2);
    octree_compute_forces(pool1, 0, b1, 2, 1.0, softening, 0.0);
    double a1 = fabs(b1[0].ax);

    // Distance d2 = 10
    Body b2[2];
    b2[0] = make_body(0, 0, 0, 0, 0, 0, 1.0);
    b2[1] = make_body(10.0, 0, 0, 0, 0, 0, 1.0);
    OctreeNode *pool2 = malloc(16 * sizeof(OctreeNode));
    int ps2 = 0;
    octree_build(pool2, &ps2, b2, 2);
    octree_compute_forces(pool2, 0, b2, 2, 1.0, softening, 0.0);
    double a2 = fabs(b2[0].ax);

    // a1/a2 should be ~(10/5)^2 = 4
    double ratio = a1 / a2;
    ASSERT_NEAR(ratio, 4.0, 0.01, "force should follow inverse-square law");

    free(pool1);
    free(pool2);
    return 1;
}

static int test_octree_vs_direct_sum(void)
{
    int n = 8;
    Body octree_bodies[8], direct_bodies[8];

    double positions[][3] = {
        {1, 2, 3}, {-4, 5, -1}, {7, -3, 2}, {0, 0, 0},
        {-2, -6, 4}, {3, 1, -5}, {-1, 8, 0}, {6, -2, -3}
    };
    double masses[] = {1, 3, 2, 5, 1, 4, 2, 3};

    for (int i = 0; i < n; i++) {
        octree_bodies[i] = make_body(positions[i][0], positions[i][1], positions[i][2],
                                     0, 0, 0, masses[i]);
        direct_bodies[i] = octree_bodies[i];
    }

    double G = 1.0, softening = 0.1;

    // Octree forces with theta=0 (exact traversal)
    OctreeNode *pool = malloc(8 * n * sizeof(OctreeNode));
    int pool_size = 0;
    octree_build(pool, &pool_size, octree_bodies, n);
    octree_compute_forces(pool, 0, octree_bodies, n, G, softening, 0.0);

    // Direct N^2 forces
    compute_direct_forces(direct_bodies, n, G, softening);

    for (int i = 0; i < n; i++) {
        ASSERT_NEAR(octree_bodies[i].ax, direct_bodies[i].ax, 1e-10, "ax mismatch vs direct");
        ASSERT_NEAR(octree_bodies[i].ay, direct_bodies[i].ay, 1e-10, "ay mismatch vs direct");
        ASSERT_NEAR(octree_bodies[i].az, direct_bodies[i].az, 1e-10, "az mismatch vs direct");
    }

    free(pool);
    return 1;
}

/* ---- integrator tests ---- */

static int test_energy_conservation(void)
{
    // Circular orbit: body orbiting a central mass
    double G = 1.0, softening = 0.01;
    double r = 5.0;
    double M = 100.0;
    double v_circ = sqrt(G * M / r); // circular velocity

    Body bodies[2];
    bodies[0] = make_body(0, 0, 0, 0, 0, 0, M);
    bodies[1] = make_body(r, 0, 0, 0, v_circ, 0, 1.0);

    OctreeNode *pool = malloc(16 * sizeof(OctreeNode));

    integrator_init_accelerations(bodies, 2, G, softening, 0.0, pool);
    double E0 = compute_total_energy(bodies, 2, G, softening);

    double dt = 0.001;
    for (int i = 0; i < 2000; i++) {
        integrator_step(bodies, 2, dt, G, softening, 0.0, pool);
    }

    double E1 = compute_total_energy(bodies, 2, G, softening);
    double rel_err = fabs((E1 - E0) / E0);

    printf("  Energy: E0=%.6f E1=%.6f rel_err=%.2e\n", E0, E1, rel_err);
    ASSERT(rel_err < 0.01, "energy should be conserved within 1%");

    free(pool);
    return 1;
}

static int test_momentum_conservation(void)
{
    int n = 4;
    Body bodies[4];
    bodies[0] = make_body(1, 0, 0, 0, 1, 0, 2.0);
    bodies[1] = make_body(-1, 0, 0, 0, -1, 0, 2.0);
    bodies[2] = make_body(0, 2, 1, -0.5, 0, 0.3, 3.0);
    bodies[3] = make_body(0, -2, -1, 0.5, 0, -0.3, 1.0);

    double G = 1.0, softening = 0.1;
    OctreeNode *pool = malloc(8 * n * sizeof(OctreeNode));
    integrator_init_accelerations(bodies, n, G, softening, 0.0, pool);

    double px0 = 0, py0 = 0, pz0 = 0;
    for (int i = 0; i < n; i++) {
        px0 += bodies[i].mass * bodies[i].vx;
        py0 += bodies[i].mass * bodies[i].vy;
        pz0 += bodies[i].mass * bodies[i].vz;
    }

    for (int i = 0; i < 500; i++) {
        integrator_step(bodies, n, 0.002, G, softening, 0.0, pool);
    }

    double px1 = 0, py1 = 0, pz1 = 0;
    for (int i = 0; i < n; i++) {
        px1 += bodies[i].mass * bodies[i].vx;
        py1 += bodies[i].mass * bodies[i].vy;
        pz1 += bodies[i].mass * bodies[i].vz;
    }

    ASSERT_NEAR(px0, px1, 1e-10, "px should be conserved");
    ASSERT_NEAR(py0, py1, 1e-10, "py should be conserved");
    ASSERT_NEAR(pz0, pz1, 1e-10, "pz should be conserved");

    free(pool);
    return 1;
}

/* ---- initial conditions tests ---- */

static int test_galaxy_body_count(void)
{
    int n = 200;
    Body *bodies = calloc(n, sizeof(Body));
    generate_spiral_galaxy(bodies, n, 5.0, 7.0, 400.0, 10.0, 0.0, 0.0);

    // Central body should be at requested center
    ASSERT_NEAR(bodies[0].x, 5.0, 1e-12, "central body x");
    ASSERT_NEAR(bodies[0].y, 7.0, 1e-12, "central body y");
    ASSERT_NEAR(bodies[0].z, 0.0, 1e-12, "central body z");
    ASSERT(bodies[0].mass > 0, "central body should have positive mass");

    // All bodies should have positive mass
    for (int i = 0; i < n; i++) {
        ASSERT(bodies[i].mass > 0, "all bodies should have positive mass");
    }

    free(bodies);
    return 1;
}

static int test_galaxy_center_of_mass(void)
{
    int n = 2000;
    double disk_radius = 20.0;
    Body *bodies = calloc(n, sizeof(Body));
    generate_spiral_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, disk_radius, 0.0, 0.0);

    double total_mass = 0, com_x = 0, com_y = 0, com_z = 0;
    for (int i = 0; i < n; i++) {
        total_mass += bodies[i].mass;
        com_x += bodies[i].mass * bodies[i].x;
        com_y += bodies[i].mass * bodies[i].y;
        com_z += bodies[i].mass * bodies[i].z;
    }
    com_x /= total_mass;
    com_y /= total_mass;
    com_z /= total_mass;

    double com_offset = sqrt(com_x*com_x + com_y*com_y + com_z*com_z);
    printf("  COM offset: %.4f (disk_radius=%.1f)\n", com_offset, disk_radius);
    ASSERT(com_offset < disk_radius * 0.1, "COM should be near origin");

    free(bodies);
    return 1;
}

/* ---- dead body tests ---- */

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

/* ---- quasar tests ---- */

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

static int test_feedback_pushes_outward(void)
{
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

/* ---- main ---- */

int main(void)
{
    printf("=== cosmosim physics tests ===\n\n");

    // Octree tests
    RUN_TEST(test_octree_single_body);
    RUN_TEST(test_octree_two_bodies_com);
    RUN_TEST(test_octree_mass_conservation);

    // Force tests
    RUN_TEST(test_two_body_force_direction);
    RUN_TEST(test_force_symmetry_newton3);
    RUN_TEST(test_force_inverse_square);
    RUN_TEST(test_octree_vs_direct_sum);

    // Integrator tests
    RUN_TEST(test_energy_conservation);
    RUN_TEST(test_momentum_conservation);

    // Initial conditions tests
    RUN_TEST(test_galaxy_body_count);
    RUN_TEST(test_galaxy_center_of_mass);

    // Dead body tests
    RUN_TEST(test_dead_body_skipping);

    // Quasar initial conditions tests
    RUN_TEST(test_quasar_galaxy_generation);

    // Quasar physics tests
    RUN_TEST(test_accretion_mass_conservation);
    RUN_TEST(test_feedback_pushes_outward);
    RUN_TEST(test_eddington_cap);
    RUN_TEST(test_jet_spawn_direction);
    RUN_TEST(test_compact_removes_dead);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
