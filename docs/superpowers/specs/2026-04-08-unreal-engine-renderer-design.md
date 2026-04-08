# Unreal Engine 5.4 Renderer — Design Spec

## Problem

cosmosim's OpenGL 3.3 renderer uses point sprites with additive blending and hand-rolled HDR bloom/lensing shaders. While functional, it cannot produce cinematic-quality visuals: no volumetric gas, no ribbon trails for jets, no physically-based lighting, and no high-quality offline rendering pipeline. The rendering is tightly coupled to the physics in a single executable.

## Goal

Render cosmosim's N-body simulation in Unreal Engine 5.4 with:
- Niagara GPU particles with per-body-type visual treatment (volumetric gas, ribbon jets, mesh SMBHs)
- UE's post-processing stack (bloom, Lumen GI, DOF, motion blur) plus custom gravitational lensing
- Real-time interactive mode and cinematic offline rendering via Sequencer + Movie Render Queue
- Existing OpenGL standalone build preserved and untouched

## Approach

Hybrid plugin architecture: the existing C physics code compiles as a shared library (`libcosmosim`) with a pure C API. A UE 5.4 plugin loads the library, runs physics on a background thread with double-buffered snapshots, and feeds particle data to Niagara via a custom Data Interface. Six Niagara systems render the six body types with distinct visual treatments.

---

## 1. C API Layer (`cosmosim_api.c/h`)

A thin C interface wrapping the existing physics into a shared library. No changes to existing source files — this is a new file that calls existing functions.

```c
typedef struct CosmosimConfig {
    int n_bodies;
    int merger;              // 0 or 1
    int quasar;              // 0 or 1
    double dt;               // timestep
    double theta;            // Barnes-Hut opening angle
    double smbh_mass_frac;   // SMBH mass fraction
    double accretion_radius;
    double jet_speed;
    double feedback_strength;
    int substeps;            // integration substeps per step
} CosmosimConfig;

typedef void* SimHandle;

SimHandle cosmosim_create(CosmosimConfig config);
void      cosmosim_step(SimHandle handle);
const Body* cosmosim_get_bodies(SimHandle handle);
int       cosmosim_get_count(SimHandle handle);        // allocated count
int       cosmosim_get_active_count(SimHandle handle);  // bodies with mass > 0
double    cosmosim_get_sim_time(SimHandle handle);  // current simulation time
void      cosmosim_destroy(SimHandle handle);
```

Internally, `cosmosim_create` allocates bodies + octree pool, calls `init_galaxy` or `init_merger`, and returns an opaque handle. `cosmosim_step` runs one full frame: octree build → gravity → SPH density → SPH forces → quasar step → integrator step → SPH cooling.

The existing `main.c` executable target is unchanged — it continues to call these functions directly (not through the API). The API exists solely for external consumers.

## 2. CMake Changes

Add a shared library target alongside the existing executable:

```cmake
# Shared library (physics only, no renderer/main)
add_library(cosmosim_lib SHARED
    src/octree.c
    src/integrator.c
    src/initial_conditions.c
    src/sph.c
    src/quasar.c
    src/cosmosim_api.c
)
target_include_directories(cosmosim_lib PUBLIC src/)
target_link_libraries(cosmosim_lib PRIVATE OpenMP::OpenMP_C m)
set_target_properties(cosmosim_lib OUTPUT_NAME cosmosim)

# Existing executable (unchanged)
add_executable(cosmosim_exe src/main.c src/renderer.c src/octree.c ...)
```

The library excludes `main.c`, `renderer.c`, all OpenGL/GLFW dependencies, and shader loading. It produces `libcosmosim.dylib` (macOS), `cosmosim.dll` (Windows), or `libcosmosim.so` (Linux).

## 3. UE Plugin Structure

```
CosmosimPlugin/
├── CosmosimPlugin.uplugin
├── Source/
│   └── CosmosimPlugin/
│       ├── CosmosimPlugin.Build.cs
│       ├── Public/
│       │   ├── CosmosimSubsystem.h
│       │   ├── NiagaraDI_Cosmosim.h
│       │   └── CosmosimController.h
│       └── Private/
│           ├── CosmosimSubsystem.cpp
│           ├── NiagaraDI_Cosmosim.cpp
│           └── CosmosimController.cpp
├── Content/
│   ├── Niagara/
│   │   ├── NS_Stars.uasset
│   │   ├── NS_Gas.uasset
│   │   ├── NS_Jets.uasset
│   │   ├── NS_SMBH.uasset
│   │   ├── NS_Lobes.uasset
│   │   └── NS_Dust.uasset
│   ├── Materials/
│   │   ├── M_Star.uasset
│   │   ├── M_Gas_Volumetric.uasset
│   │   ├── M_Jet_Ribbon.uasset
│   │   ├── M_SMBH_EventHorizon.uasset
│   │   └── PP_GravLensing.uasset
│   └── Maps/
│       └── CosmosimDefault.umap
└── ThirdParty/
    └── libcosmosim/
        ├── include/   (cosmosim_api.h, body.h)
        └── lib/       (pre-built .dylib/.dll/.so)
```

`CosmosimPlugin.Build.cs` adds the ThirdParty include/lib paths and links `libcosmosim`. Module dependencies: `Niagara`, `NiagaraCore`, `RenderCore`, `RHI`, `EnhancedInput`.

## 4. UCosmosimSubsystem

A `UGameInstanceSubsystem` that owns the simulation lifecycle.

**Startup:**
1. Load `libcosmosim` via `FPlatformProcess::GetDllHandle`
2. Resolve function pointers (`cosmosim_create`, `cosmosim_step`, etc.)
3. Create simulation with configurable `CosmosimConfig` (exposed as UPROPERTY settings)
4. Allocate two Body snapshot buffers (A and B), each `n_alloc * sizeof(Body)`
5. Spawn physics thread

**Physics thread loop:**
```
while (running):
    cosmosim_step(handle)
    memcpy(writeBuffer, cosmosim_get_bodies(handle), count * sizeof(Body))
    atomic_swap(writeBuffer, readBuffer)
    active_count.store(cosmosim_get_active_count(handle))
```

**Game thread access:**
- `GetReadBuffer()` returns the current read buffer pointer (safe to read while physics writes to the other)
- `GetActiveCount()` returns the atomically-stored active count
- `Tick()` on game thread does NOT run physics — it only updates SMBH data for the post-process material (positions, masses, luminosities as dynamic material parameters)

**Thread safety:** The double buffer uses `std::atomic<int>` as a buffer index. Physics thread writes to `buffers[writeIdx]`, then swaps `writeIdx ^= 1`. Game thread reads from `buffers[writeIdx ^ 1]`. The memcpy is the synchronization point — a single full-frame snapshot, no partial reads.

**Blueprint-exposed controls:**
- `Pause()` / `Resume()`
- `Reset(CosmosimConfig)`
- `SetSubstepRate(int)`
- `GetSimulationTime() → float`

## 5. Custom Niagara Data Interface (`UNiagaraDI_Cosmosim`)

Inherits `UNiagaraDataInterface`. Provides read access to the Body snapshot buffer for Niagara GPU simulation.

**Exposed functions (callable from Niagara graph):**

| Function | Inputs | Outputs | Purpose |
|----------|--------|---------|---------|
| `GetNumBodies` | — | Int | Total active body count |
| `GetBodyPosition` | Index | Vector3 | World position |
| `GetBodyVelocity` | Index | Vector3 | Velocity (for Doppler, color) |
| `GetBodyMass` | Index | Float | Mass (for sizing, brightness) |
| `GetBodyType` | Index | Int | BodyType enum value |
| `GetBodyTemperature` | Index | Float | internal_energy (gas coloring) |
| `GetBodyDensity` | Index | Float | SPH density (gas opacity/size) |
| `GetBodyLifetime` | Index | Float | Jet remaining lifetime |
| `GetBodyLuminosity` | Index | Float | SMBH luminosity |
| `GetBodySpinAxis` | Index | Vector3 | SMBH spin (disk orientation) |

**Implementation:** Each function indexes into the read buffer (`GetReadBuffer()[index]`) and converts the relevant `double` fields to `float` for Niagara. The NDI registers a GPU data provider that uploads the buffer as a structured buffer for GPU particle access.

**Per-frame update:** The NDI's `PerInstanceTickFunction` checks the atomic frame counter. If a new snapshot is available, it uploads the Body data to a GPU structured buffer via `RHICreateStructuredBuffer` / `RHIUpdateStructuredBuffer`. The upload is a single bulk memcpy of `active_count * sizeof(NiagaraBodyData)` where `NiagaraBodyData` is a packed float struct (52 bytes per body: 3 pos + 1 mass + 3 vel + 1 type + 1 temp + 1 density + 1 lifetime + 1 luminosity + 3 spin_axis = 13 floats, padded to 16 floats / 64 bytes for GPU alignment).

## 6. Niagara Systems

Each body type has a dedicated Niagara system. All systems share the same NDI instance and filter by `GetBodyType()`.

### NS_Stars (BODY_STAR = 0)
- **Renderer:** Sprite (gaussian soft circle texture)
- **Size:** 1–8 units, `lerp(1, 8, mass / max_mass)`
- **Color:** Velocity magnitude mapped to blue→white→yellow→orange ramp
- **Emissive:** `mass * 2.0` — contributes to Lumen GI
- **LOD:** Alpha fades to 0 beyond camera distance threshold

### NS_Gas (BODY_GAS = 1)
- **Renderer:** Sprite with volumetric soft-particle depth fade (translucent, no depth write)
- **Size:** Proportional to `smoothing_h` via density: `base_size / (density + 0.1)`. Fluffy when sparse, compact when dense
- **Color:** Temperature ramp matching current shader: cold purple (#331a66) → orange (#ff6619) → hot blue-white (#f8f8ff). Breakpoint at `log(internal_energy) = 0.5` on normalized scale
- **Opacity:** `clamp(density / ref_density, 0.1, 0.8)` — denser gas is more opaque
- **Emissive:** `internal_energy * 3.0` — hot shocked gas glows brightly, cold gas is dim
- **Sub-UV:** 4-frame flipbook for cloud texture variation (randomized per particle)

### NS_Jets (BODY_JET = 3)
- **Renderer:** Ribbon trail (primary) + sprite overlay for knots
- **Ribbon:** Control points from sequential jet particle positions. Width 0.5–2.0 units
- **Color:** Relativistic Doppler shift. Camera-relative velocity dot product determines blue-shift vs red-shift. Beaming factor `D = 1 / (gamma * (1 - beta * cos(theta)))`, intensity scales as `D^3`
- **Emissive:** High intensity (`mass * 10.0`), major bloom source
- **Opacity:** `lifetime / max_lifetime` — fades as jet particles age
- **Knots:** Particles with `mass > knot_threshold` rendered as larger bright sprites on top of ribbon

### NS_SMBH (BODY_SMBH = 2)
- **Renderer:** Static mesh (dark sphere) + separate emitter for photon ring + accretion disk
- **Event horizon mesh:** Black unlit sphere, radius = Schwarzschild radius derived from mass
- **Photon ring:** Thin emissive torus at 1.5× Schwarzschild radius. Doppler-asymmetric brightness (brighter on approaching side). Oriented perpendicular to spin axis
- **Accretion disk:** Flat ring of small emissive particles orbiting in the spin plane. Temperature-colored (same ramp as gas). Only visible when `accretion_rate > threshold`
- **Luminosity halo:** Additive sprite scaled by `luminosity`, large bloom radius

### NS_Lobes (BODY_LOBE = 5)
- **Renderer:** Large diffuse sprite
- **Size:** 5–15 units, scaled by mass
- **Color:** Orange-red (#ff6633), high emissive
- **Bloom:** Major contributor — represents jet termination hotspots

### NS_Dust (BODY_DUST = 4)
- **Renderer:** Small faint sprite
- **Size:** 0.5–2 units
- **Color:** Warm grey-brown (#8b7355)
- **Opacity:** Low (0.1–0.3) — tidal stream particles are wispy
- **Emissive:** Minimal — lit primarily by nearby stars/gas via Lumen

## 7. Post-Processing

### Gravitational Lensing (`PP_GravLensing`)

Custom post-process material applied via a post-process volume in the level.

**Inputs (set as dynamic material parameters by `UCosmosimSubsystem::Tick`):**
- `SMBHCount` (int): number of active SMBHs (max 2)
- `SMBHScreenPos[2]` (Vector2): screen-space positions of each SMBH
- `SMBHMass[2]` (float): mass of each SMBH
- `SchwarzschildRadius[2]` (float): event horizon radius in screen units
- `LensingStrength[2]` (float): `mass * 0.002` (matching current shader)
- `SMBHSpinScreen[2]` (Vector2): projected spin axis for frame-dragging asymmetry

**Shader logic (ported from current `composite.frag`):**
1. For each SMBH, compute distance from current pixel to SMBH screen position
2. Apply Einstein deflection angle: `deflection = lensing_strength / distance`
3. Offset UV by deflection direction × strength
4. Sample scene texture at displaced UV (4x multisampled for quality)
5. Event horizon shadow: darken pixels within Schwarzschild radius (soft edge)
6. Photon ring overlay: thin bright ring at ~1.5× Schwarzschild radius with Doppler asymmetry from spin projection

### UE Built-in Post-Processing

Configured via post-process volume in `CosmosimDefault.umap`:
- **Bloom:** Intensity 0.8, threshold 1.0 (emissive particles trigger it naturally)
- **Lumen GI:** Enabled — emissive particles cast indirect light
- **Exposure:** Auto-exposure with min/max brightness matching current arithmetic-mean formula
- **DOF:** Disabled in real-time mode, enabled for cinematic (configurable focal distance)
- **Motion blur:** Disabled in real-time mode, enabled for cinematic captures

## 8. Camera System (`ACosmosimController`)

An `APlayerController` subclass managing camera input and modes.

### Orbit Mode (default)
- Spherical coordinates (azimuth, elevation, distance) around a target point
- Left-drag: rotate (azimuth + elevation)
- Right-drag: pan target
- Scroll: zoom distance
- Matches current OpenGL camera behavior exactly

### Free Camera Mode
- WASD + mouse look
- Speed adjustable with scroll
- Toggle via keybind

### Track Mode
- Auto-follows SMBH midpoint (or center of mass if no SMBH)
- Exponential smoothing: `target = 0.97 * target + 0.03 * smbh_midpoint` (matching current implementation)
- Distance adapts to SMBH separation: clamped to [12, 40] units
- Toggle via keybind

### Sequencer Integration
- Camera actor can be possessed by a Level Sequence for cinematic shots
- Camera rails, cuts, dolly moves via UE Sequencer
- Movie Render Queue for high-quality offline frame output (replaces PPM offline renderer)

## 9. HUD

Minimal `UUserWidget` overlay:
- Active particle count / allocated count
- Simulation time (from `cosmosim_get_sim_time`)
- Physics substep rate (steps/sec)
- Pause indicator
- Toggle visibility with H key

## 10. Performance Considerations

- **Double buffer memcpy:** ~4MB per frame at 100k bodies × 40 bytes. Well within memory bandwidth
- **Niagara GPU upload:** Single structured buffer update per frame, ~4MB. Standard for large Niagara systems
- **Physics thread:** Runs independently of render framerate. If physics is slower than render, Niagara re-reads the same snapshot (no stutter, just temporal lag)
- **Particle LOD:** Stars and dust fade at distance to reduce overdraw. Gas and jets always render (they're the visual focus)
- **Target:** 60fps real-time at 50k particles, 30fps at 100k particles (GPU-dependent)

## 11. Out of Scope

- Moving physics to GPU / compute shaders (physics stays CPU, C code)
- Multiplayer / networking
- VR support
- Sound / audio
- UE Chaos physics integration (we use our own gravity + SPH)
- Incremental octree updates (rebuild-per-frame is fast enough)
- Star formation, MHD, metallicity (same exclusions as SPH spec)
