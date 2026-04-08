# Level Setup Guide

This guide covers setting up the default Cosmosim level (`CosmosimDefault`) with all rendering, post-processing, and UI components.

---

## 1. Create the Level

1. File > New Level > **Empty Level**.
2. Save as `Content/Maps/CosmosimDefault`.

---

## 2. Delete Default Lighting

- If a **Directional Light** exists in the level, **delete it**. Space scenes have no global directional light.
- Delete any default **Sky Sphere**, **Sky Light**, or **Atmospheric Fog** actors.

---

## 3. Dark Skybox

1. Create or import a **dark space cubemap** (mostly black with faint stars).
   - A 1024x1024 per-face cubemap with subtle star field works well.
   - Alternatively, use a solid black cubemap for pure darkness.
2. Add a **Sky Sphere** or **Static Mesh** (inverted sphere) to the level.
3. Apply the space cubemap material to it.
4. Alternatively, set the level's **Sky Atmosphere** to disabled and use a simple **Background Color** of `#000000` in the project settings.

---

## 4. Post-Process Volume

Add a **Post Process Volume** to the level with the following settings:

### Infinite Extent
- Check **Infinite Extent (Unbound)** so it applies everywhere.

### Bloom
- **Bloom Intensity:** `0.8`
- **Bloom Threshold:** `1.0`
- This creates the soft glow around bright particles (stars, jets, photon rings) without over-blooming dim particles.

### Auto Exposure (Histogram)
- **Metering Mode:** Auto Exposure Histogram
- **Min Brightness:** `0.5`
- **Max Brightness:** `10.0`
- **Speed Up:** `1.0`
- **Speed Down:** `1.0`
- This allows the camera to adapt when viewing bright SMBHs vs. dim dust regions.

### Motion Blur
- **Motion Blur Amount:** `0.0` (disabled)
- Motion blur makes particles look smeared and is undesirable for a point-particle simulation.

### Depth of Field
- **Focal Distance:** `0.0`
- **DOF Method:** None / disabled
- Everything in the simulation should be in focus.

### Global Illumination
- **Lumen Global Illumination:** Enabled
- Lumen will pick up emissive particle light and bounce it subtly. Not strictly necessary but adds atmosphere.

### Post-Process Materials
- Add **PP_GravLensing** to the Post Process Materials array.
- Set the Blendable to **Before Tonemapping**.
- This enables gravitational lensing around SMBHs.

---

## 5. Game Mode

### SpectatorPawn
- The default camera should be a **SpectatorPawn** (or subclass) for free-flying camera movement.
- Set the default pawn in the Game Mode.

### CosmosimController
- Create or assign **CosmosimController** as the Player Controller class.
- The controller handles:
  - Simulation play/pause (P key)
  - Speed control
  - Projecting SMBH positions to screen space for `PP_GravLensing` parameters
  - HUD visibility toggle (H key)

### Game Mode Setup
1. Create a new **Game Mode Blueprint** (or use `BP_CosmosimGameMode`).
2. Set **Default Pawn Class** to `SpectatorPawn`.
3. Set **Player Controller Class** to `CosmosimController`.
4. In World Settings for the level, set **Game Mode Override** to this game mode.

---

## 6. Place Niagara System Actors

Place 6 **Niagara System** actors in the level, one for each body type:

| Actor Name | Niagara System Asset |
|------------|---------------------|
| NS_Stars_Actor | NS_Stars |
| NS_Gas_Actor | NS_Gas |
| NS_SMBH_Actor | NS_SMBH |
| NS_Jets_Actor | NS_Jets |
| NS_Dust_Actor | NS_Dust |
| NS_Lobes_Actor | NS_Lobes |

- Place all actors at **world origin** `(0, 0, 0)`. The data interface provides world-space positions; the Niagara systems should not be offset.
- Set all actors to **Auto Activate** so they begin rendering when the level starts.
- All 6 systems share the same `UNiagaraDataInterfaceCosmosim` reading from the single simulation buffer.

---

## 7. HUD Widget Blueprint

Create a **Widget Blueprint** named `WBP_CosmosimHUD` in `Content/UI/`.

### Layout
The HUD uses a simple overlay with text in the top-left corner:

```
Particles: 20000 / 20000
Sim Time: 1.234 s
[PAUSED]
```

### Widgets

| Widget | Type | Binding | Notes |
|--------|------|---------|-------|
| ParticleCountText | Text Block | `ActiveCount / TotalCount` | Top-left, font size 16, white |
| SimTimeText | Text Block | `SimTime` formatted to 3 decimal places | Below particle count |
| PausedText | Text Block | "PAUSED" | Centered, font size 24, yellow, visible only when paused |

### Bindings
- `ParticleCountText`: bind to `cosmosim_get_active_count()` / `cosmosim_get_count()` via the controller.
- `SimTimeText`: bind to `cosmosim_get_sim_time()` formatted as `"Sim Time: %.3f s"`.
- `PausedText`: visibility bound to a `bIsPaused` boolean on the controller.

### Creating the HUD
1. Right-click in Content Browser > User Interface > Widget Blueprint.
2. Name it `WBP_CosmosimHUD`.
3. Add a **Canvas Panel** as root.
4. Add text blocks as described above, anchored to top-left.
5. In `CosmosimController::BeginPlay`, create the widget and add to viewport:
   ```cpp
   HUDWidget = CreateWidget<UUserWidget>(this, WBP_CosmosimHUDClass);
   HUDWidget->AddToViewport();
   ```

---

## 8. Sequencer (Offline Rendering)

For cinematic captures and GIF/video output:

### Level Sequence
1. Window > Cinematics > **Add Level Sequence**.
2. Save as `Content/Cinematics/LS_CosmosimCapture`.
3. Add a **Camera Cut Track**.
4. Add a **CineCamera Actor** to the level, position it with a good view of the simulation.
5. In the Camera Cut Track, assign the CineCamera.
6. Set sequence length to match desired capture duration.

### Camera Animation
- Add keyframes for the CineCamera's transform to create orbital or fly-through camera moves.
- Use **Smooth Interpolation** on the transform track for fluid motion.

### Movie Render Queue
1. Window > Cinematics > **Movie Render Queue**.
2. Add the Level Sequence (`LS_CosmosimCapture`).
3. Configure output:
   - **Output Format:** PNG Sequence (for GIFs) or ProRes/EXR (for video).
   - **Resolution:** 1920x1080 or 3840x2160.
   - **Anti-Aliasing:** Spatial Sample Count 4, Temporal Sample Count 1.
   - **Console Variables:** `r.MotionBlurQuality 0` (ensure no motion blur).
4. Click **Render (Local)** to begin offline capture.

### Tips
- Pause the simulation and advance frame-by-frame for deterministic captures.
- Use `cosmosim_step()` calls from a Sequencer event track to advance the simulation at a fixed rate per frame.
- For GIF conversion, use ffmpeg: `ffmpeg -framerate 30 -i frame.%04d.png -vf "scale=640:-1" output.gif`.

---

## Quick Checklist

- [ ] Empty level saved as `CosmosimDefault`
- [ ] Default directional light deleted
- [ ] Dark skybox/cubemap applied
- [ ] Post Process Volume: bloom 0.8, histogram exposure 0.5-10, no motion blur, no DOF, Lumen GI on
- [ ] PP_GravLensing added to post-process materials
- [ ] Game Mode set with SpectatorPawn + CosmosimController
- [ ] 6 Niagara System actors placed at origin
- [ ] WBP_CosmosimHUD widget blueprint created
- [ ] Level Sequence and Movie Render Queue configured (optional, for offline capture)
