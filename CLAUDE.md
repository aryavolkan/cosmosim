# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

cosmosim is a real-time 2D N-body gravity simulator written in C11 using the Barnes-Hut algorithm for O(N log N) force computation. It renders particles via OpenGL 3.3 with GLFW/GLAD.

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

No test suite exists. Verify changes by running the simulation visually.

## Dependencies

- **GLFW 3.3+**: auto-fetched via CMake FetchContent if not installed
- **GLAD**: bundled in `glad/` (OpenGL loader)
- **OpenMP**: optional, used to parallelize force computation (`quadtree.c:169`)

## Architecture

The simulation loop in `main.c` runs: build quadtree → compute forces → leapfrog integrate → render.

| Module | Purpose |
|---|---|
| `body.h` | `Body` struct: position, velocity, acceleration, mass (all `double`) |
| `quadtree.c/h` | Barnes-Hut quadtree using a flat pool allocator (array of `QuadTreeNode`, pre-allocated as `8*n`). `quadtree_build` constructs the tree; `quadtree_compute_forces` walks it with the opening-angle criterion (theta) |
| `integrator.c/h` | Kick-drift-kick (leapfrog) symplectic integrator. Each frame runs `SUBSTEPS` (2) integration steps |
| `renderer.c/h` | OpenGL point-sprite renderer with additive blending. Uploads body positions+masses each frame as `GL_STREAM_DRAW`. Shaders loaded from `src/shaders/` at runtime via `SHADER_DIR` compile define |
| `initial_conditions.c/h` | Generates exponential-disk spiral galaxies with circular orbital velocities, or a two-galaxy merger. Uses xorshift64 PRNG |
| `src/shaders/` | GLSL 330: vertex shader sets point size from mass; fragment shader colors by mass (blue→orange) with gaussian falloff |

## Key Design Details

- Physics uses `double` precision; renderer converts to `float` for GPU upload.
- The quadtree pool is allocated once (`8*n` nodes) and rebuilt from scratch every substep — no incremental updates.
- `SHADER_DIR` is baked in at compile time via CMake `add_definitions`, pointing to the source tree. Shaders are read from disk at startup, not embedded.
- Camera uses orthographic projection with pan (left-drag) and zoom (scroll).
