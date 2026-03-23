# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

cosmosim is a real-time 3D N-body gravity simulator written in C11 using the Barnes-Hut algorithm (octree) for O(N log N) force computation. It renders particles via OpenGL 3.3 with GLFW/GLAD.

## Build Commands

```bash
# Configure + build (from project root)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Run
./build/cosmosim                    # 20k bodies, single galaxy
./build/cosmosim -n 50000           # custom body count
./build/cosmosim -m                 # galaxy merger mode
./build/cosmosim -dt 0.002 -t 0.7  # custom timestep and theta
```

```bash
# Run tests
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics

# Or via CTest
cd build && ctest
```

Tests cover octree construction, force computation (Newton's 3rd law, inverse-square, octree-vs-direct), integrator conservation laws (energy, momentum), and initial conditions. No GPU/display needed for tests.

## Dependencies

- **GLFW 3.3+**: auto-fetched via CMake FetchContent if not installed
- **GLAD**: bundled in `glad/` (OpenGL loader)
- **OpenMP**: optional, used to parallelize force computation (`octree.c:169`)

## Architecture

The simulation loop in `main.c` runs: build octree → compute forces → leapfrog integrate → render.

| Module | Purpose |
|---|---|
| `body.h` | `Body` struct: 3D position, velocity, acceleration, mass (all `double`) |
| `octree.c/h` | Barnes-Hut octree (8 children per node) using a flat pool allocator (array of `OctreeNode`, pre-allocated as `8*n`). `octree_build` constructs the tree; `octree_compute_forces` walks it with the opening-angle criterion (theta) |
| `integrator.c/h` | Kick-drift-kick (leapfrog) symplectic integrator. Each frame runs `SUBSTEPS` (2) integration steps |
| `renderer.c/h` | OpenGL point-sprite renderer with additive blending and perspective projection. Orbit camera with spherical coordinates (azimuth/elevation/distance). Uploads body positions+masses (4 floats per body) each frame as `GL_STREAM_DRAW`. Shaders loaded from `src/shaders/` at runtime via `SHADER_DIR` compile define |
| `initial_conditions.c/h` | Generates exponential-disk spiral galaxies with circular orbital velocities and vertical dispersion, or a two-galaxy merger. Uses xorshift64 PRNG |
| `src/shaders/` | GLSL 330: vertex shader applies view+projection transforms with perspective point-size attenuation; fragment shader colors by mass (blue→orange) with gaussian falloff |

## Key Design Details

- Physics uses `double` precision; renderer converts to `float` for GPU upload.
- The octree pool is allocated once (`8*n` nodes) and rebuilt from scratch every substep — no incremental updates.
- `SHADER_DIR` is baked in at compile time via CMake `add_definitions`, pointing to the source tree. Shaders are read from disk at startup, not embedded.
- Camera uses perspective projection with orbit (left-drag), pan (right/middle-drag), zoom (scroll), and reset (R key). Depth testing is disabled in favor of additive blending for the glowing particle look.
