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
    if (u1 < 1e-15)
        u1 = 1e-15;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void generate_spiral_galaxy(Body *bodies,
                            int n,
                            double cx,
                            double cy,
                            double galaxy_mass,
                            double disk_radius,
                            double vx_bulk,
                            double vy_bulk)
{
    rng_seed((uint64_t)time(NULL));

    double scale_radius = disk_radius / 4.0;
    double scale_height = disk_radius * 0.02; // thin disk vertical extent
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

    // Disk bodies
    for (int i = 1; i < n; i++) {
        // Exponential radial distribution
        double u = rng_uniform();
        if (u < 1e-15)
            u = 1e-15;
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
        // Circular orbital velocity: v = sqrt(G * M_enclosed / r)
        // M_enclosed approximation for exponential disk
        double m_enclosed = galaxy_mass * (1.0 - exp(-r / scale_radius) * (1.0 + r / scale_radius));
        // Add central mass contribution
        m_enclosed += bodies[0].mass;
        if (r < 1e-10)
            r = 1e-10;
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
    generate_spiral_galaxy(bodies,
                           n1,
                           -separation * 0.5,
                           0.0,
                           galaxy_mass,
                           disk_radius,
                           approach_vel,
                           approach_vel * 0.3);

    // Galaxy 2: right, moving left and slightly down
    // Re-seed for different structure
    rng_seed((uint64_t)time(NULL) + 12345);
    generate_spiral_galaxy(bodies + n1,
                           n2,
                           separation * 0.5,
                           0.0,
                           galaxy_mass * 0.7,
                           disk_radius * 0.8,
                           -approach_vel,
                           -approach_vel * 0.3);
}

void generate_quasar_galaxy(Body *bodies,
                            int n,
                            double cx,
                            double cy,
                            double galaxy_mass,
                            double disk_radius,
                            double vx_bulk,
                            double vy_bulk,
                            double smbh_mass_frac)
{
    // Generate base galaxy
    generate_spiral_galaxy(bodies, n, cx, cy, galaxy_mass, disk_radius, vx_bulk, vy_bulk);

    // Upgrade central body to SMBH
    bodies[0].mass = galaxy_mass * smbh_mass_frac;
    bodies[0].type = BODY_SMBH;
    // Tilt spin axis so jets are visible in the camera plane (not edge-on)
    bodies[0].spin_x = 0.0;
    bodies[0].spin_y = 0.7071;
    bodies[0].spin_z = 0.7071;
    // Seed initial accretion so jets start immediately
    bodies[0].accretion_rate = galaxy_mass * 0.001;
    bodies[0].luminosity = 0.1 * bodies[0].accretion_rate;

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

void generate_merger_dust(Body *bodies, int start_idx, int n_dust, double separation)
{
    rng_seed((uint64_t)time(NULL) + 99999ULL);

    double disk_radius = separation * 0.15;
    /* Approximate galaxy mass from the body count (matches generate_quasar_merger) */
    double galaxy_mass = (double)(start_idx / 2) * 2.0;
    double galaxy_mass2 = galaxy_mass * 1.2;
    double disk_r2 = disk_radius * 1.3;

    double cx1 = -separation * 0.5;
    double cx2 = separation * 0.5;

    int n_tidal = n_dust / 3;
    int n_halo1 = (n_dust - n_tidal) / 2;
    int n_halo2 = n_dust - n_tidal - n_halo1;

    /* Tidal bridge: elongated dust cloud connecting the two galaxy centers */
    for (int i = 0; i < n_tidal; i++) {
        int idx = start_idx + i;
        double t = rng_uniform();
        double bx = cx1 + t * (cx2 - cx1);
        double by = rng_gaussian() * separation * 0.06;
        bodies[idx].x = bx + rng_gaussian() * separation * 0.04;
        bodies[idx].y = by;
        bodies[idx].z = rng_gaussian() * separation * 0.015;
        bodies[idx].mass = 0.4 + rng_uniform() * 0.6;
        bodies[idx].type = BODY_DUST;
        /* Orbital velocity interpolated from each galaxy's rotation */
        double r = sqrt(bx * bx + by * by);
        if (r < 1e-10)
            r = 1e-10;
        double v_c = sqrt(galaxy_mass / r) * 0.25;
        bodies[idx].vx = -v_c * by / r + rng_gaussian() * 0.4;
        bodies[idx].vy = v_c * bx / r + rng_gaussian() * 0.4;
        bodies[idx].vz = rng_gaussian() * 0.2;
    }

    /* Outer disk halo: galaxy 1 (left) */
    for (int i = 0; i < n_halo1; i++) {
        int idx = start_idx + n_tidal + i;
        double r = disk_radius * (0.7 + 0.9 * sqrt(rng_uniform()));
        double theta = 2.0 * M_PI * rng_uniform();
        bodies[idx].x = cx1 + r * cos(theta);
        bodies[idx].y = r * sin(theta);
        bodies[idx].z = rng_gaussian() * disk_radius * 0.08;
        bodies[idx].mass = 0.4 + rng_uniform() * 0.6;
        bodies[idx].type = BODY_DUST;
        double v_c = sqrt(galaxy_mass / r) * 0.75;
        double disp = v_c * 0.08;
        bodies[idx].vx = -v_c * sin(theta) + rng_gaussian() * disp;
        bodies[idx].vy = v_c * cos(theta) + rng_gaussian() * disp;
        bodies[idx].vz = rng_gaussian() * disp * 0.3;
    }

    /* Outer disk halo: galaxy 2 (right) */
    for (int i = 0; i < n_halo2; i++) {
        int idx = start_idx + n_tidal + n_halo1 + i;
        double r = disk_r2 * (0.7 + 0.9 * sqrt(rng_uniform()));
        double theta = 2.0 * M_PI * rng_uniform();
        bodies[idx].x = cx2 + r * cos(theta);
        bodies[idx].y = r * sin(theta);
        bodies[idx].z = rng_gaussian() * disk_r2 * 0.08;
        bodies[idx].mass = 0.4 + rng_uniform() * 0.6;
        bodies[idx].type = BODY_DUST;
        double v_c = sqrt(galaxy_mass2 / r) * 0.75;
        double disp = v_c * 0.08;
        bodies[idx].vx = -v_c * sin(theta) + rng_gaussian() * disp;
        bodies[idx].vy = v_c * cos(theta) + rng_gaussian() * disp;
        bodies[idx].vz = rng_gaussian() * disp * 0.3;
    }
}

void generate_quasar_merger(
    Body *bodies, int n, double separation, double approach_vel, double smbh_mass_frac)
{
    int n1 = n / 2;
    int n2 = n - n1;

    double disk_radius = separation * 0.15;
    double galaxy_mass = (double)n1 * 2.0;
    // Andromeda/Milky Way mass ratio: M31 is ~1.2x MW mass
    double galaxy2_mass_ratio = 1.2;
    double total_mass = galaxy_mass + galaxy_mass * galaxy2_mass_ratio;

    // Compute orbital velocity for a decaying merger encounter.
    // MW-Andromeda approach is nearly radial (v_tangential ~ 0.17 * v_radial)
    // Use 0.2 * v_circ for small tangential component — produces one
    // grazing pass with tidal tails then rapid merger via dynamical friction.
    double v_circ = sqrt(total_mass / separation);
    double v_orbit = v_circ * 0.2;

    // Galaxy 1: left, moving up (tangential)
    // Small radial component for slight infall, large tangential for orbit
    double vx1 = approach_vel; // mild radial approach
    double vy1 = v_orbit;      // strong tangential motion

    // Galaxy 2: right, moving down (opposite tangential)
    double vx2 = -approach_vel;
    double vy2 = -v_orbit * 0.85; // slightly less (MW lighter than M31)

    generate_quasar_galaxy(
        bodies, n1, -separation * 0.5, 0.0, galaxy_mass, disk_radius, vx1, vy1, smbh_mass_frac);

    // Galaxy 1 SMBH spin: aligned with disk normal (+z, tilted into view)
    bodies[0].spin_x = 0.0;
    bodies[0].spin_y = 0.7071;
    bodies[0].spin_z = 0.7071;

    // Galaxy 2: re-seed RNG for different structure
    rng_seed((uint64_t)time(NULL) + 12345);
    generate_quasar_galaxy(bodies + n1,
                           n2,
                           separation * 0.5,
                           0.0,
                           galaxy_mass * galaxy2_mass_ratio,
                           disk_radius * 1.3,
                           vx2,
                           vy2,
                           smbh_mass_frac);

    // Tilt Galaxy 2's disk ~50° around the X-axis relative to Galaxy 1.
    // This creates the inclined encounter geometry seen in MW-Andromeda.
    // Rotation matrix Rx(50°): y' = y*cos - z*sin, z' = y*sin + z*cos
    {
        double tilt = 50.0 * M_PI / 180.0;
        double ct = cos(tilt), st = sin(tilt);

        for (int i = n1; i < n1 + n2; i++) {
            // Translate to galaxy center, rotate, translate back
            double y0 = bodies[i].y;
            double z0 = bodies[i].z;
            bodies[i].y = y0 * ct - z0 * st;
            bodies[i].z = y0 * st + z0 * ct;

            // Rotate velocities
            double vy0 = bodies[i].vy;
            double vz0 = bodies[i].vz;
            bodies[i].vy = vy0 * ct - vz0 * st;
            bodies[i].vz = vy0 * st + vz0 * ct;
        }

        // Galaxy 2 SMBH spin: tilted disk normal
        // Original disk normal is (0, 0.7071, 0.7071), rotate by same tilt
        double sy = 0.7071 * ct - 0.7071 * st;
        double sz = 0.7071 * st + 0.7071 * ct;
        double smag = sqrt(sy * sy + sz * sz);
        bodies[n1].spin_x = 0.0;
        bodies[n1].spin_y = sy / smag;
        bodies[n1].spin_z = sz / smag;
    }
}
