# Materials Setup Guide

This guide covers all materials and the post-process gravitational lensing material used by the Cosmosim Niagara systems.

---

## M_Star

Glowing point-sprite material for star particles.

### Properties
- **Blend Mode:** Additive
- **Shading Model:** Unlit
- **Two Sided:** Yes

### Graph
1. **Texture Sample** - gaussian radial gradient (white center, transparent edges). Use a 128x128 radial gradient texture or generate procedurally with `RadialGradientExponential`.
2. **Radial Gradient** (procedural alternative):
   - `TexCoord[0]` centered: `UV = TexCoord - 0.5`
   - `dist = length(UV) * 2.0`
   - `gaussian = exp(-dist * dist * 4.0)`
   - Output to Opacity.
3. **Emissive Color:**
   - `ParticleColor.rgb * EmissiveStrength`
   - `EmissiveStrength` is a **Scalar Parameter** (default 1.0), driven per-particle from Niagara.
4. **Opacity:**
   - `gaussian * ParticleColor.a`

### Parameters
| Parameter | Type | Default | Driven By |
|-----------|------|---------|-----------|
| EmissiveStrength | Scalar | 1.0 | Niagara dynamic parameter (mass * 2.0) |

---

## M_Gas_Volumetric

Translucent cloud material for SPH gas particles.

### Properties
- **Blend Mode:** Translucent
- **Shading Model:** Unlit
- **Two Sided:** Yes
- **Disable Depth Test:** No
- **Output Velocity:** Off (no motion blur on gas)

### Graph
1. **Depth Fade:**
   - Use the `DepthFade` node with **Fade Distance = 50 units**.
   - This prevents hard edges where gas sprites intersect geometry or other particles.
2. **Cloud Noise (SubUV 2x2):**
   - Sample a **2x2 SubUV flipbook texture** (4 cloud variations in a 2x2 grid).
   - Use `SubUV_Function` or manually compute UVs: `SubUVIndex` selects which quadrant.
   - Each gas particle picks a random SubUV index at spawn.
3. **Emissive Color:**
   - `ParticleColor.rgb * EmissiveStrength`
   - `EmissiveStrength` is a Scalar Parameter (default 1.0), driven by `temperature * 3.0`.
4. **Opacity:**
   - `CloudTexture.a * DepthFade * ParticleColor.a`
   - Final opacity is clamped by the Niagara-driven `ParticleColor.a` (which encodes the density-based opacity).

### Parameters
| Parameter | Type | Default | Driven By |
|-----------|------|---------|-----------|
| EmissiveStrength | Scalar | 1.0 | Niagara (temperature * 3.0) |
| SubUVIndex | Scalar | 0 | Niagara (random 0-3) |
| FadeDistance | Scalar | 50.0 | Constant |

---

## M_Jet_Ribbon

Additive ribbon material for relativistic jet trails.

### Properties
- **Blend Mode:** Additive
- **Shading Model:** Unlit
- **Two Sided:** Yes
- **Used with Niagara Ribbons:** Yes (check this in Material Details)

### Graph
1. **V-Axis Edge Fade:**
   - The ribbon's V texture coordinate runs 0 to 1 across the ribbon width.
   - Compute edge fade: `EdgeFade = 1.0 - abs(TexCoord.V * 2.0 - 1.0)`
   - Apply power curve for sharper falloff: `EdgeFade = pow(EdgeFade, 2.0)`
   - This makes ribbon edges transparent and center bright.
2. **Emissive Color:**
   - `ParticleColor.rgb * EmissiveStrength * EdgeFade`
   - `EmissiveStrength` default: **5.0** (jets are very bright).
3. **Opacity:**
   - `EdgeFade * ParticleColor.a`

### Parameters
| Parameter | Type | Default | Driven By |
|-----------|------|---------|-----------|
| EmissiveStrength | Scalar | 5.0 | Niagara (mass * 10.0) |

---

## M_SMBH_EventHorizon

Perfectly black sphere material for the event horizon.

### Properties
- **Blend Mode:** Opaque
- **Shading Model:** Unlit
- **Two Sided:** No

### Graph
- **Emissive Color:** Constant `(0, 0, 0)` - pure black.
- No other inputs. The event horizon is a featureless black sphere that occludes everything behind it.

---

## PP_GravLensing

Post-process material that simulates gravitational lensing around SMBHs. Applied to the Post Process Volume in the level.

### Properties
- **Material Domain:** Post Process
- **Blendable Location:** Before Tonemapping

### Dynamic Material Parameters

These parameters are set at runtime by `ACosmosimController` or a blueprint each frame:

| Parameter | Type | Description |
|-----------|------|-------------|
| SMBHCount | Scalar | Number of active SMBHs (0, 1, or 2) |
| SMBHScreenPos_0 | Vector2 | Screen-space UV of first SMBH |
| SMBHScreenPos_1 | Vector2 | Screen-space UV of second SMBH |
| LensingStrength_0 | Scalar | Lensing strength of first SMBH (proportional to mass) |
| LensingStrength_1 | Scalar | Lensing strength of second SMBH |
| SchwarzschildRadius_0 | Scalar | Screen-space Schwarzschild radius of first SMBH |
| SchwarzschildRadius_1 | Scalar | Screen-space Schwarzschild radius of second SMBH |

### Custom HLSL Node

Create a **Custom** expression node in the material graph with the following code. Connect `SceneTexture:PostProcessInput0` as input, and output the result to **Emissive Color**.

Input pins:
- `SceneUV` (float2) - connect to `ScreenPosition` node
- `SMBHCount` (float) - scalar parameter
- `SMBHScreenPos0` (float2) - vector parameter .xy
- `SMBHScreenPos1` (float2) - vector parameter .xy
- `LensingStrength0` (float) - scalar parameter
- `LensingStrength1` (float) - scalar parameter
- `SchwarzschildRadius0` (float) - scalar parameter
- `SchwarzschildRadius1` (float) - scalar parameter

```hlsl
// PP_GravLensing - Gravitational lensing post-process
// Simulates Einstein deflection, event horizon shadow, and photon ring

float2 UV = SceneUV;
float2 TotalOffset = float2(0.0, 0.0);
float ShadowMask = 1.0;
float3 PhotonRing = float3(0.0, 0.0, 0.0);

// Aspect ratio correction for circular lensing
float AspectRatio = View.ViewSizeAndInvSize.x / View.ViewSizeAndInvSize.y;

// Process each SMBH
for (int i = 0; i < 2; i++)
{
    if (i >= (int)SMBHCount) break;

    float2 SMBHPos = (i == 0) ? SMBHScreenPos0 : SMBHScreenPos1;
    float Strength = (i == 0) ? LensingStrength0 : LensingStrength1;
    float Rs = (i == 0) ? SchwarzschildRadius0 : SchwarzschildRadius1;

    // Direction from SMBH to current pixel (aspect-corrected)
    float2 Delta = UV - SMBHPos;
    Delta.x *= AspectRatio;
    float Dist = length(Delta);
    float2 Dir = (Dist > 0.0001) ? Delta / Dist : float2(0, 0);

    // === Einstein deflection ===
    // Deflection angle ~ 4GM/(c^2 * b) ~ Strength / Dist
    // Clamp minimum distance to avoid singularity
    float ClampedDist = max(Dist, Rs * 0.5);
    float Deflection = Strength / (ClampedDist * ClampedDist);
    float2 Offset = -Dir * Deflection;
    Offset.x /= AspectRatio; // undo aspect correction for UV space
    TotalOffset += Offset;

    // === Event horizon shadow ===
    // Smoothstep shadow from 0.7 to 1.2 Schwarzschild radii
    float ShadowFactor = smoothstep(0.7 * Rs, 1.2 * Rs, Dist);
    ShadowMask *= ShadowFactor;

    // === Photon ring overlay ===
    // Bright ring at 1.5x Schwarzschild radius (photon sphere)
    float RingDist = abs(Dist - 1.5 * Rs);
    float RingWidth = Rs * 0.15;
    float RingIntensity = exp(-RingDist * RingDist / (RingWidth * RingWidth));
    RingIntensity *= Strength * 50.0; // scale with SMBH mass
    PhotonRing += float3(1.0, 0.95, 0.8) * RingIntensity;
}

// === 4x multisampled UV displacement ===
// Sample scene color at 4 offset positions for smoother lensing
float2 SampleOffsets[4] = {
    float2( 0.5,  0.5),
    float2(-0.5,  0.5),
    float2( 0.5, -0.5),
    float2(-0.5, -0.5)
};

float TexelSize = View.ViewSizeAndInvSize.z; // 1/width
float3 LensedColor = float3(0, 0, 0);

for (int s = 0; s < 4; s++)
{
    float2 SampleUV = UV + TotalOffset + SampleOffsets[s] * TexelSize;
    SampleUV = clamp(SampleUV, 0.001, 0.999);
    LensedColor += SceneTextureLookup(SampleUV, 14, false).rgb;
}
LensedColor *= 0.25; // average 4 samples

// Composite: lensed scene * shadow mask + photon ring
float3 FinalColor = LensedColor * ShadowMask + PhotonRing;

return FinalColor;
```

### Setup Steps

1. Create a new Material, set Domain to **Post Process**.
2. Set Blendable Location to **Before Tonemapping**.
3. Create all 7 parameters listed above as Material Parameter nodes.
4. Add a **Custom** node, paste the HLSL code above.
5. Connect all parameter nodes to the Custom node's input pins.
6. Connect `ScreenPosition` (no offset) to the `SceneUV` input.
7. Connect the Custom node output to **Emissive Color**.
8. In `ACosmosimController::Tick`, project each SMBH world position to screen UV and update the dynamic material instance parameters.

---

## Texture Assets Required

| Texture | Resolution | Description |
|---------|-----------|-------------|
| T_Gaussian | 128x128 | White radial gaussian, alpha = gaussian falloff |
| T_CloudSubUV | 256x256 | 2x2 flipbook (4 cloud noise variations), RGBA |
| T_Ring | 128x128 | Thin bright ring on transparent background |
| T_DiskGradient | 256x64 | Radial gradient for accretion disk (white inner, orange outer) |
