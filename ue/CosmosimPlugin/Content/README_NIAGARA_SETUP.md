# Niagara System Setup Guide

This guide covers creating all 6 Niagara particle systems for the Cosmosim UE plugin. Each system renders a different `BodyType` from the simulation.

| System | BodyType | Value |
|--------|----------|-------|
| NS_Stars | BODY_STAR | 0 |
| NS_Gas | BODY_GAS | 1 |
| NS_SMBH | BODY_SMBH | 2 |
| NS_Jets | BODY_JET | 3 |
| NS_Dust | BODY_DUST | 4 |
| NS_Lobes | BODY_LOBE | 5 |

---

## Common Setup (All Systems)

Every Niagara system shares the same base configuration:

1. **Create Empty Niagara System** - right-click in Content Browser > FX > Niagara System > Empty System.
2. **Simulation Target** - set to **CPU Sim**. The particle positions come from the C simulation, not GPU compute.
3. **Fixed Bounds** - set Fixed Bounds to **200 units** in all axes. Disable dynamic bounds to avoid per-frame recalculation.
4. **Data Interface** - add the **"Cosmosim Particles" Data Interface** (UNiagaraDataInterfaceCosmosim) to the system. This exposes `GetPosition`, `GetVelocity`, `GetMass`, `GetBodyType`, `GetDensity`, `GetInternalEnergy`, `GetLifetime`, `GetLuminosity`, `GetAccretionRate`, `GetSmoothingH`, and `GetSpinAxis`.
5. **Spawn Module** - use **Spawn Burst Instantaneous** with Count driven by `ExecutionIndex`. Bind to the data interface's `GetCount` for the matching body type.
6. **Filter by Body Type** - in the Particle Update stack, add a **GetBodyType** call and kill any particle where the type does not match the system's target value.
7. **Update Position** - each frame, read `GetPosition(ExecutionIndex)` from the data interface and write directly to `Particles.Position`. Do NOT use velocity integration in Niagara; the C simulation handles physics.

---

## NS_Stars (Type = 0)

Stars are the most numerous particles. They use a glowing point-sprite look.

### Renderer
- **Sprite Renderer** with `M_Star` material.
- Alignment: Camera-facing.

### Texture
- Gaussian radial gradient texture (bright center, soft falloff). A 64x64 or 128x128 white gaussian is sufficient.

### Size
- Map mass to sprite size: `SpriteSize = lerp(1.0, 8.0, saturate(mass / maxStarMass))`.
- Typical mass range is 0.0001 to 0.01; normalize accordingly.

### Color
- Velocity-mapped color ramp based on `length(velocity)`:
  - Low velocity: **Blue** `#4466ff`
  - Medium velocity: **White** `#ffffff`
  - High velocity: **Yellow** `#ffdd44`
  - Very high velocity: **Orange** `#ff8800`
- Use a `Sample Curve` module with the velocity magnitude as input.

### Emissive
- `EmissiveStrength = mass * 2.0`
- Feed into the material's emissive multiplier parameter.

### LOD
- **LOD Fade**: fade opacity to 0 when the particle is beyond a configurable distance (default 500 units). Use `DistanceFade = 1.0 - saturate((dist - FadeStart) / FadeRange)`.

---

## NS_Gas (Type = 1)

Gas particles represent SPH fluid. They appear as translucent volumetric blobs.

### Renderer
- **Sprite Renderer** with `M_Gas_Volumetric` material.
- Alignment: Camera-facing.
- **No depth write** (translucent, blended behind stars).

### Size
- Size inversely proportional to density: `SpriteSize = BaseSize / (density + 0.1)`.
- `BaseSize` default: 3.0. Dense gas appears compact; diffuse gas appears large.

### Color (Temperature Ramp)
- Color is driven by `internal_energy` (temperature proxy).
- Use `log(internal_energy)` as the ramp parameter with breakpoint at **0.5**:
  - Cold (log(u) < 0.0): **Purple** `#331a66`
  - Warm (log(u) ~ 0.5): **Orange** `#ff6619`
  - Hot (log(u) > 1.0): **Blue-white** `#f8f8ff`
- Implement as a `Sample Color Curve` with 3 keys at positions 0.0, 0.5, and 1.0 on the normalized log(u) range.

### Opacity
- `Opacity = clamp(density / 0.5, 0.1, 0.8)`
- Dense gas is more opaque; diffuse gas is nearly transparent.

### Emissive
- `EmissiveStrength = temperature * 3.0` (where temperature = internal_energy).

### SubUV
- Use a **2x2 SubUV flipbook** (4 frames) for cloud variation.
- Set SubUV mode to **Random** so each particle picks a random cloud frame at spawn.

---

## NS_Jets (Type = 3)

Relativistic jet particles launched from SMBHs. They use ribbon trails with Doppler effects.

### Renderer
- **Ribbon Renderer** for the trail + **Sprite Renderer** for knot highlights.
- Material: `M_Jet_Ribbon` (additive, unlit).
- Ribbon width: 0.5 units.

### Doppler Coloring
Compute the Doppler factor for relativistic beaming:

```
beta = length(velocity) / c_sim       // normalized speed
gamma = 1.0 / sqrt(1.0 - beta*beta)
cos_theta = dot(normalize(velocity), normalize(cameraDirection))
D = 1.0 / (gamma * (1.0 - beta * cos(theta)))
```

- Apply **D^3 beaming** to brightness: `Brightness = D * D * D`.
- The approaching jet appears bright blue-white; the receding jet appears dim red.

### Opacity
- `Opacity = lifetime / maxLifetime`
- Jets fade out as their remaining lifetime decreases to 0.
- `maxLifetime` should match the jet spawn lifetime from the simulation config.

### Emissive
- `EmissiveStrength = mass * 10.0`
- Jets are extremely bright relative to their mass.

### Trail
- Ribbon trail length: 8-16 knots.
- Trail points decay over 0.5 seconds.

---

## NS_SMBH (Type = 2)

Supermassive black holes are rendered as multi-component objects: event horizon, photon ring, accretion disk, and luminosity halo.

### Event Horizon (Black Sphere)
- **Mesh Renderer** using a sphere mesh.
- Material: `M_SMBH_EventHorizon` (opaque, unlit, constant black `#000000`).
- Sphere radius = **Schwarzschild radius** = `mass * 0.01` (in sim units).
- Depth write ON so the black hole occludes background particles.

### Photon Ring
- **Sprite Renderer** with a torus/ring texture.
- Radius: `1.5 * Schwarzschild_radius`.
- Color: bright white-yellow `#ffffcc`.
- Emissive: `luminosity * 5.0`.
- Oriented perpendicular to the spin axis (`spin_x, spin_y, spin_z`).

### Accretion Disk
- **Sprite Renderer** with a disk/ring texture.
- Inner radius: `3.0 * Schwarzschild_radius`.
- Outer radius: `10.0 * Schwarzschild_radius`.
- Color: orange-white gradient from inner (hot, white) to outer (cooler, orange).
- Opacity modulated by `accretion_rate`.
- Oriented perpendicular to the spin axis.

### Luminosity Halo
- **Sprite Renderer** with gaussian texture (same as stars).
- Size: `20.0 * sqrt(luminosity)`.
- Color: warm white `#fff8e0`.
- Emissive: `luminosity * 2.0`.
- Additive blend.

---

## NS_Lobes (Type = 5)

Radio lobes are large diffuse regions inflated by jet feedback at the ends of the jet axis.

### Renderer
- **Sprite Renderer** with gaussian or soft cloud texture.
- Alignment: Camera-facing.

### Size
- Large sprites: `SpriteSize = lerp(5.0, 15.0, saturate(mass / maxLobeMass))`.

### Color
- Solid **orange-red**: `#ff6633`.

### Emissive
- `EmissiveStrength = mass * 3.0`.
- Lobes are emissive but not as bright as jets.

### Opacity
- 0.4 - 0.7 range. Lobes are semi-transparent to show structure behind them.

---

## NS_Dust (Type = 4)

Dust particles are small, faint, and numerous. They fill the galactic disk and obscure background light.

### Renderer
- **Sprite Renderer** with small gaussian or noise texture.
- Alignment: Camera-facing.

### Size
- Small sprites: `SpriteSize = lerp(0.5, 2.0, saturate(mass / maxDustMass))`.

### Color
- Warm grey-brown: `#8b7355`.

### Opacity
- Very faint: `Opacity = lerp(0.1, 0.3, saturate(density / maxDustDensity))`.

### Emissive
- `EmissiveStrength = 0.0` (dust does not emit light; it only scatters/absorbs).

---

## Performance Notes

- All 6 systems share one `UNiagaraDataInterfaceCosmosim` instance reading from the same simulation buffer.
- CPU sim is required because particle positions are written from C code, not computed on GPU.
- Fixed bounds prevent Niagara from doing per-frame AABB recalculation.
- For scenes with > 50k particles, consider enabling **Distance Culling** on NS_Stars and NS_Dust at 1000 units.
- The data interface performs a single memcpy of the body array per frame; individual particle reads are pointer offsets with no additional copies.
