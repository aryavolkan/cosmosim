# Quasar Simulation Design

## Overview

Add realistic quasar simulation to cosmosim: supermassive black holes (SMBHs) with accretion disks, relativistic jets with AGN feedback, and Eddington-limited self-regulation. Two modes: single-galaxy AGN and merger-triggered quasar. Rendering adds HDR bloom, jet streaks, accretion disk glow, and screen-space gravitational lensing.

**Approach**: Extend `Body` with a type enum and per-type physics branching. The octree treats all bodies uniformly for gravity; accretion, feedback, and jet logic run as additional passes around the existing force computation. Rendering moves to a multi-pass framebuffer pipeline.

---

## 1. Body Type System & SMBH

### Body struct extension

```c
typedef enum { BODY_STAR, BODY_GAS, BODY_SMBH, BODY_JET } BodyType;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double ax, ay, az;
    double mass;
    BodyType type;
    double spin_x, spin_y, spin_z;  // SMBH spin axis (normalized), jet direction
    double accretion_rate;           // SMBH: current mass inflow rate
    double luminosity;               // SMBH: derived from accretion_rate
} Body;
```

Extra fields cost ~48 bytes per body but are only meaningful for the 1-2 SMBHs. Not worth a separate struct since the octree and integrator iterate a flat array.

### SMBH behavior

- **Accretion radius** `r_acc` (configurable, default ~3.0 sim units). Bodies entering this radius become accretion candidates.
- **Eddington luminosity** `L_edd = k * M_smbh` where `k` is a tunable constant calibrated for visual/dynamic effect.
- **Spin axis** initialized perpendicular to galaxy disk (0, 0, 1). For mergers, each SMBH gets its galaxy's disk normal. Spin axis slowly precesses based on accreted angular momentum.

---

## 2. Accretion Mechanics

Runs after force computation, before the integrator drift step. For each body within `r_acc` of an SMBH:

1. Compute **specific angular momentum** `L = r x v` relative to the SMBH.
2. Compute **circularization radius** `r_circ = |L|^2 / (G * M_smbh)`.

### Three outcomes

- **Disk capture** (`r_circ > r_isco`, body not already tightly bound): Body transitions to `BODY_GAS`. Keeps angular momentum but receives a drag force `F_drag = -alpha * v_radial` (viscosity parameter `alpha` ~ 0.01-0.1) that spirals it inward. `r_isco` (innermost stable circular orbit) is a tunable parameter, default ~0.5 sim units — bodies that spiral inside this radius fall in directly.

- **Direct accretion** (`r < r_swallow`, ~0.1-0.5 units): Body absorbed. Mass and momentum added to SMBH. Angular momentum nudges SMBH spin axis. Body marked dead (mass = 0).

- **Hyperbolic flyby**: No special handling; normal gravity provides deflection.

### Accretion rate and luminosity

- `accretion_rate`: running exponential average of mass swallowed per unit time.
- `luminosity = eta_eff * accretion_rate` (tunable radiative efficiency).
- Drives both feedback strength and rendering brightness.

### Dead body handling

- Bodies with `mass = 0` skipped in force computation and rendering.
- Compact the array periodically (every N frames or when dead count > 10%).
- New jet particles reuse dead slots first.

---

## 3. Jet Mechanics & AGN Feedback

### Jet spawning (each substep when `luminosity > 0`)

- Spawn along `+/- spin_axis` from SMBH position.
- Rate proportional to luminosity: `n_jet = floor(luminosity / jet_energy_per_particle)`, capped to prevent runaway.
- Each jet particle (`BODY_JET`):
  - Position: SMBH + small random perpendicular offset (cone half-angle ~5-10 degrees)
  - Velocity: `v_jet * spin_axis` (bipolar). `v_jet` ~ 10-50x local orbital velocity.
  - Mass: small (~0.1-1.0) — carries kinetic energy without dominating gravity.
  - Lifetime: decays after set time (becomes dead body).

### AGN feedback (radiation pressure)

Bodies within `r_feedback` (~2-3x `r_acc`) receive outward radial force:

```
F_feedback = (luminosity / (4 * pi * r^2)) * sigma_eff * r_hat
```

Creates self-regulation loop:
1. Matter falls in -> accretion rate rises -> luminosity rises
2. Luminosity drives feedback -> pushes matter away -> accretion rate drops
3. Luminosity drops -> feedback weakens -> matter falls in again

### Eddington limit

If accretion would push luminosity above `L_edd`, excess energy goes into stronger jets rather than more luminosity. Caps feedback force but increases jet power.

### Jet-environment interaction

Jet particles are real bodies in the array, participating in octree gravity. Kinetic momentum transfer to nearby bodies naturally creates cocoons and cavities.

---

## 4. Initial Conditions

### Single-galaxy AGN mode (`-q` / `--quasar`)

- Generate spiral galaxy via existing `generate_spiral_galaxy`.
- Replace central massive body with `BODY_SMBH`: mass = 5% of galaxy mass, spin = (0,0,1).
- Inner ~20% of disk radius bodies initialized as `BODY_GAS` (pre-seeds accretion disk for immediate activity).
- Remaining bodies stay `BODY_STAR`.

### Merger-triggered quasar mode (`-m -q`)

- Generate two galaxies via existing `generate_merger`.
- Each galaxy's central body becomes `BODY_SMBH` with spin (0,0,1).
- No pre-seeded gas — collision dynamics naturally funnel material inward.
- Tidal stripping triggers quasar ignition organically.
- Dual SMBH binary with dual jets possible as galaxies merge.

### New CLI flags

| Flag | Default | Description |
|------|---------|-------------|
| `-q` / `--quasar` | off | Enable quasar physics |
| `--smbh-mass <f>` | 0.05 | SMBH mass as fraction of galaxy mass |
| `--accretion-radius <f>` | 3.0 | Accretion capture radius |
| `--jet-speed <f>` | 20.0 | Jet particle velocity |
| `--feedback-strength <f>` | 1.0 | Radiation pressure multiplier |
| `--high-fidelity` | off | 4x substeps, 2x jet spawning, enhanced rendering |

---

## 5. Rendering Pipeline

Current single-pass pipeline becomes a multi-pass HDR framebuffer pipeline.

### Pass 1: Scene render to HDR FBO (GL_RGBA16F)

Three shader programs drawn into the same FBO:

**Star/gas shader** (evolved from current `particle.vert/frag`):
- `BODY_STAR`: current blue-to-orange mass gradient
- `BODY_GAS`: red-orange-white gradient based on proximity to SMBH (closer = hotter = whiter)
- `BODY_SMBH`: solid bright white, large point size
- Accretion disk enhancement: gas bodies near SMBH get boosted brightness proportional to `1/r^2` via uniforms (`smbh_pos`, `smbh_luminosity`)

**Jet shader** (`jet.vert/frag`):
- `BODY_JET` particles rendered as elongated streaks along velocity vector
- Color: blue-white core with cyan edges
- HDR values >1.0 for strong bloom pickup

**Data upload**: Expand from 4 to 8 floats per body: (x, y, z, mass, vx, vy, vz, type_as_float). Velocity needed for jet streak orientation, type for shader branching.

### Pass 2: Bloom extraction + blur

- Brightness-threshold pass: extract pixels with luminance > 1.0 to half-resolution bloom FBO
- Two-pass Gaussian blur (horizontal + vertical), 2-3 iterations
- SMBH core, inner accretion disk, and jets bloom; stars stay sharp

### Pass 3: Gravitational lensing (screen-space)

- Full-screen quad shader reading HDR scene texture
- Per-fragment: compute screen-space distance to SMBH projected position
- Radial UV displacement: `uv_offset = strength * (M_smbh / r_screen^2) * r_hat`
- Creates Einstein-ring-like distortion near the SMBH
- Cheap: single texture sample + math per fragment

### Pass 4: Composite to screen

- Full-screen quad combining `HDR_scene + bloom_intensity * bloom_texture`
- Tonemap HDR to LDR via Reinhard: `color / (1 + color)`
- Output to default framebuffer

---

## 6. Performance & Adaptive Quality

### Cost analysis

- Accretion/feedback: O(N) scan per substep — negligible
- Jet particles capped at 10% of initial N (2000 for 20k sim)
- Bloom blur: ~1-2ms at 1080p. Passes 2-4 are full-screen quads (sub-millisecond each)

### Adaptive quality tiers

| Setting | Default | `--high-fidelity` |
|---------|---------|-------------------|
| Substeps/frame | 2 | 8 |
| Jet spawn rate | 1x | 2x |
| Jet particle cap | 10% of N | 25% of N |
| Bloom blur iterations | 2 | 4 |
| Lensing samples | 1 | 4 (supersampled) |
| Array compaction | every 120 frames | every 60 frames |

### Memory

- Pre-allocate body array with headroom: `malloc((n + n/10) * sizeof(Body))` for jet particle growth.
- Dead body compaction: O(N), runs infrequently.

### Testing additions

- **Accretion**: mass/momentum conservation when a body is swallowed
- **Feedback**: force direction is radial outward, magnitude scales with luminosity
- **Jets**: spawned particles have correct velocity direction and magnitude
- **Eddington cap**: luminosity does not exceed `L_edd`
- **Lensing shader**: UV displacement formula validated against known values
