# cosmosim

Real-time 3D N-body gravity simulator using the Barnes-Hut algorithm. Renders tens of thousands of gravitationally interacting particles as a spiral galaxy or galaxy merger.

![C](https://img.shields.io/badge/C11-grey?logo=c) ![OpenGL](https://img.shields.io/badge/OpenGL%203.3-blue?logo=opengl)

## Features

- **Barnes-Hut octree** for O(N log N) force computation
- **Symplectic leapfrog integrator** (kick-drift-kick) for energy-conserving time evolution
- **OpenMP parallelization** of force calculations
- **3D perspective rendering** with orbit camera and additive-blend point sprites
- Mass-dependent particle color (blue-white → orange) and size
- Two simulation modes: single spiral galaxy and galaxy merger

## Building

Requires CMake 3.16+ and a C11 compiler. GLFW is fetched automatically if not found on the system.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```
./build/cosmosim [options]

Options:
  -n <count>    Number of bodies (default 20000)
  -m, --merger  Galaxy merger mode (two colliding galaxies)
  -dt <value>   Timestep (default 0.005)
  -t <theta>    Barnes-Hut opening angle (default 0.5, lower = more accurate)
```

### Controls

| Input | Action |
|-------|--------|
| Left-click drag | Orbit camera |
| Right/middle-click drag | Pan |
| Scroll wheel | Zoom in/out |
| Space | Pause/resume |
| R | Reset camera |
| Q / Esc | Quit |

### Examples

```bash
# Default: 20k-body spiral galaxy
./build/cosmosim

# Large simulation
./build/cosmosim -n 100000

# Galaxy merger with 50k bodies
./build/cosmosim -m -n 50000

# High accuracy (smaller theta = less approximation)
./build/cosmosim -t 0.3
```

## Testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics
# Or via CTest:
cd build && ctest
```

Tests cover octree construction, force computation (Newton's 3rd law, inverse-square, octree-vs-direct comparison), integrator conservation laws (energy, momentum), and initial conditions generation. No GPU/display required.

## Architecture

| Module | Purpose |
|--------|---------|
| `body.h` | `Body` struct: 3D position, velocity, acceleration, mass (all `double`) |
| `octree.c/h` | Barnes-Hut octree with flat pool allocator (`8*n` nodes). Rebuilt from scratch each substep |
| `integrator.c/h` | Kick-drift-kick leapfrog, 2 substeps per frame |
| `renderer.c/h` | OpenGL point-sprite renderer, orbit camera (spherical coords), additive blending, depth test disabled |
| `initial_conditions.c/h` | Exponential-disk spiral galaxies with xorshift64 PRNG, or two-galaxy merger |
| `src/shaders/` | GLSL 330: mass-based coloring (blue->orange) with gaussian falloff, perspective point-size attenuation |

Physics uses `double` precision; renderer converts to `float` for GPU upload. Shaders loaded from disk at runtime via `SHADER_DIR` compile define (set by CMake to `src/shaders/`).

## How It Works

Each frame the simulation:

1. **Builds an octree** over all body positions in 3D, computing center-of-mass summaries at each node
2. **Computes gravitational forces** by walking the tree — distant clusters are approximated as point masses when they subtend an angle smaller than theta (the Barnes-Hut criterion)
3. **Integrates motion** using a symplectic leapfrog scheme (2 substeps per frame) that conserves energy over long timescales
4. **Renders** all bodies as OpenGL point sprites with perspective projection and additive blending — color shifts from blue-white (low mass) to orange (high mass)

Initial conditions place bodies in an exponential disk with circular orbital velocities derived from the enclosed mass profile, plus a thin vertical dispersion. In merger mode, two galaxies are set on a collision course with offset trajectories.
