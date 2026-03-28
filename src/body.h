#ifndef BODY_H
#define BODY_H

typedef enum {
    BODY_STAR = 0,
    BODY_GAS = 1,
    BODY_SMBH = 2,
    BODY_JET = 3,
    BODY_DUST = 4,
    BODY_LOBE = 5
} BodyType;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double ax, ay, az;
    double mass;
    BodyType type;
    double spin_x, spin_y, spin_z; // SMBH: normalized spin/jet axis
    double accretion_rate;         // SMBH: exponential avg mass inflow
    double luminosity;             // SMBH: eta_eff * accretion_rate
    double lifetime;               // JET: remaining lifetime (seconds)
} Body;

#endif
