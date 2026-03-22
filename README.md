# cosmosim

Real-time 2D N-body gravity simulator using the Barnes-Hut algorithm. Renders tens of thousands of gravitationally interacting particles as a spiral galaxy or galaxy merger.

![C](https://img.shields.io/badge/C11-grey?logo=c) ![OpenGL](https://img.shields.io/badge/OpenGL%203.3-blue?logo=opengl)

## Features

- **Barnes-Hut quadtree** for O(N log N) force computation
- **Symplectic leapfrog integrator** (kick-drift-kick) for energy-conserving time evolution
- **OpenMP parallelization** of force calculations
- **Additive-blend point-sprite rendering** with mass-dependent color and size
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
| Scroll wheel | Zoom in/out |
| Left-click drag | Pan |
| Space | Pause/resume |
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

## How It Works

Each frame the simulation:

1. **Builds a quadtree** over all body positions, computing center-of-mass summaries at each node
2. **Computes gravitational forces** by walking the tree — distant clusters are approximated as point masses when they subtend an angle smaller than theta (the Barnes-Hut criterion)
3. **Integrates motion** using a symplectic leapfrog scheme (2 substeps per frame) that conserves energy over long timescales
4. **Renders** all bodies as OpenGL point sprites with additive blending — color shifts from blue-white (low mass) to orange (high mass)

Initial conditions place bodies in an exponential disk with circular orbital velocities derived from the enclosed mass profile. In merger mode, two galaxies are set on a collision course with offset trajectories.
