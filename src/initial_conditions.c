#include "initial_conditions.h"
#include <math.h>
#include <stdint.h>
#include <time.h>

// xorshift64 PRNG
static uint64_t rng_state;

static void rng_seed(uint64_t seed)
{
    rng_state = seed ? seed : 1;
}

static uint64_t rng_next(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static double rng_uniform(void)
{
    return (rng_next() >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
}

// Box-Muller transform
static double rng_gaussian(void)
{
    double u1 = rng_uniform();
    double u2 = rng_uniform();
    if (u1 < 1e-15) u1 = 1e-15;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void generate_spiral_galaxy(Body *bodies, int n, double cx, double cy,
                            double galaxy_mass, double disk_radius,
                            double vx_bulk, double vy_bulk)
{
    rng_seed((uint64_t)time(NULL));

    double scale_radius = disk_radius / 4.0;
    double scale_height = disk_radius * 0.02; // thin disk vertical extent
    double total_mass = 0.0;

    // Central massive body
    bodies[0].x = cx;
    bodies[0].y = cy;
    bodies[0].z = 0.0;
    bodies[0].vx = vx_bulk;
    bodies[0].vy = vy_bulk;
    bodies[0].vz = 0.0;
    bodies[0].ax = 0.0;
    bodies[0].ay = 0.0;
    bodies[0].az = 0.0;
    bodies[0].mass = galaxy_mass * 0.01;
    total_mass += bodies[0].mass;

    // Disk bodies
    for (int i = 1; i < n; i++) {
        // Exponential radial distribution
        double u = rng_uniform();
        if (u < 1e-15) u = 1e-15;
        double r = -scale_radius * log(1.0 - u * (1.0 - exp(-disk_radius / scale_radius)));
        double theta = 2.0 * M_PI * rng_uniform();

        bodies[i].x = cx + r * cos(theta);
        bodies[i].y = cy + r * sin(theta);
        bodies[i].z = rng_gaussian() * scale_height;

        // Mass: mostly uniform, a few heavy bodies
        if (rng_uniform() < 0.01) {
            bodies[i].mass = 10.0 + rng_uniform() * 90.0;
        } else {
            bodies[i].mass = 1.0;
        }
        total_mass += bodies[i].mass;

        // Circular orbital velocity: v = sqrt(G * M_enclosed / r)
        // M_enclosed approximation for exponential disk
        double m_enclosed = galaxy_mass * (1.0 - exp(-r / scale_radius) * (1.0 + r / scale_radius));
        // Add central mass contribution
        m_enclosed += bodies[0].mass;
        if (r < 1e-10) r = 1e-10;
        double v_circ = sqrt(m_enclosed / r); // G=1 in sim units

        // Tangential velocity + dispersion
        double dispersion = 0.05 * v_circ;
        double vt = v_circ + rng_gaussian() * dispersion;
        double vr = rng_gaussian() * dispersion;

        bodies[i].vx = vx_bulk - vt * sin(theta) + vr * cos(theta);
        bodies[i].vy = vy_bulk + vt * cos(theta) + vr * sin(theta);
        bodies[i].vz = rng_gaussian() * dispersion * 0.1;
        bodies[i].ax = 0.0;
        bodies[i].ay = 0.0;
        bodies[i].az = 0.0;
    }
}

void generate_merger(Body *bodies, int n, double separation, double approach_vel)
{
    int n1 = n / 2;
    int n2 = n - n1;

    double disk_radius = separation * 0.15;
    double galaxy_mass = (double)n1 * 2.0;

    // Galaxy 1: left, moving right and slightly up
    generate_spiral_galaxy(bodies, n1,
                           -separation * 0.5, 0.0,
                           galaxy_mass, disk_radius,
                           approach_vel, approach_vel * 0.3);

    // Galaxy 2: right, moving left and slightly down
    // Re-seed for different structure
    rng_seed((uint64_t)time(NULL) + 12345);
    generate_spiral_galaxy(bodies + n1, n2,
                           separation * 0.5, 0.0,
                           galaxy_mass * 0.7, disk_radius * 0.8,
                           -approach_vel, -approach_vel * 0.3);
}

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
