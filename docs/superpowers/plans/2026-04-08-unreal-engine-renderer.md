# Unreal Engine 5.4 Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render cosmosim's N-body simulation in Unreal Engine 5.4 via a hybrid plugin that loads the C physics as a shared library and feeds particle data to Niagara through a custom Data Interface.

**Architecture:** The existing C physics code gets a thin API wrapper (`cosmosim_api.c/h`) and builds as a shared library alongside the existing executable. A UE 5.4 plugin loads this library, runs physics on a background thread with double-buffered snapshots, and exposes particle data to six per-body-type Niagara systems via a custom `UNiagaraDataInterface`. Post-processing handles gravitational lensing.

**Tech Stack:** C11 (physics library), C++ / UE 5.4 (plugin), Niagara (GPU particles), HLSL (materials/post-process), CMake (C build), UBT (UE build)

**Spec:** `docs/superpowers/specs/2026-04-08-unreal-engine-renderer-design.md`

---

## File Structure

### C Side (existing repo)

| File | Action | Responsibility |
|------|--------|----------------|
| `src/cosmosim_api.h` | Create | Public C API header — config struct, opaque handle, function declarations |
| `src/cosmosim_api.c` | Create | API implementation — allocates sim state, delegates to existing physics functions |
| `CMakeLists.txt` | Modify | Add `cosmosim_shared` SHARED library target |
| `tests/test_physics.c` | Modify | Add API layer tests |

### UE Plugin (new directory in repo)

| File | Action | Responsibility |
|------|--------|----------------|
| `ue/CosmosimPlugin/CosmosimPlugin.uplugin` | Create | Plugin descriptor |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/CosmosimPlugin.Build.cs` | Create | UBT build rules, links libcosmosim |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimModule.h` | Create | Module interface |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimModule.cpp` | Create | Module startup/shutdown |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimSubsystem.h` | Create | Game instance subsystem — physics thread, double buffer |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimSubsystem.cpp` | Create | Subsystem implementation |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/NiagaraDI_Cosmosim.h` | Create | Custom Niagara Data Interface header |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/NiagaraDI_Cosmosim.cpp` | Create | NDI implementation — GPU buffer upload, Niagara function bindings |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimController.h` | Create | Player controller — camera modes, HUD |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimController.cpp` | Create | Controller implementation |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimHUD.h` | Create | UMG widget for stats overlay |
| `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimHUD.cpp` | Create | HUD implementation |
| `ue/CosmosimPlugin/ThirdParty/libcosmosim/include/` | Create | Copied headers (cosmosim_api.h, body.h) |
| `ue/CosmosimPlugin/ThirdParty/libcosmosim/lib/` | Create | Pre-built library binaries |

### UE Content (created in editor, documented here for reference)

| Asset | Type | Purpose |
|-------|------|---------|
| `Content/Niagara/NS_Stars` | Niagara System | Star particle rendering |
| `Content/Niagara/NS_Gas` | Niagara System | Volumetric gas rendering |
| `Content/Niagara/NS_Jets` | Niagara System | Jet ribbon trails |
| `Content/Niagara/NS_SMBH` | Niagara System | Black hole mesh + accretion disk |
| `Content/Niagara/NS_Lobes` | Niagara System | Jet lobe hotspots |
| `Content/Niagara/NS_Dust` | Niagara System | Tidal stream dust |
| `Content/Materials/M_Star` | Material | Gaussian emissive sprite |
| `Content/Materials/M_Gas_Volumetric` | Material | Translucent volumetric sprite |
| `Content/Materials/M_Jet_Ribbon` | Material | Emissive ribbon with Doppler |
| `Content/Materials/M_SMBH_EventHorizon` | Material | Unlit black sphere |
| `Content/Materials/PP_GravLensing` | Post-Process Material | Screen-space gravitational lensing |
| `Content/Maps/CosmosimDefault` | Level | Default level with post-process volume |

---

## Task 1: C API Header (`cosmosim_api.h`)

**Files:**
- Create: `src/cosmosim_api.h`

- [ ] **Step 1: Create the API header**

```c
#ifndef COSMOSIM_API_H
#define COSMOSIM_API_H

#include "body.h"

#ifdef _WIN32
  #ifdef COSMOSIM_BUILDING_DLL
    #define COSMOSIM_API __declspec(dllexport)
  #else
    #define COSMOSIM_API __declspec(dllimport)
  #endif
#else
  #define COSMOSIM_API __attribute__((visibility("default")))
#endif

typedef struct {
    int n_bodies;
    int merger;              /* 0 or 1 */
    int quasar;              /* 0 or 1 */
    double dt;               /* timestep */
    double theta;            /* Barnes-Hut opening angle */
    double smbh_mass_frac;   /* SMBH mass fraction (quasar mode) */
    double accretion_radius;
    double jet_speed;
    double feedback_strength;
    int substeps;            /* integration substeps per cosmosim_step call */
} CosmosimConfig;

typedef void* SimHandle;

COSMOSIM_API CosmosimConfig cosmosim_default_config(void);
COSMOSIM_API SimHandle cosmosim_create(CosmosimConfig config);
COSMOSIM_API void cosmosim_step(SimHandle handle);
COSMOSIM_API const Body* cosmosim_get_bodies(SimHandle handle);
COSMOSIM_API int cosmosim_get_count(SimHandle handle);
COSMOSIM_API int cosmosim_get_active_count(SimHandle handle);
COSMOSIM_API double cosmosim_get_sim_time(SimHandle handle);
COSMOSIM_API void cosmosim_destroy(SimHandle handle);

#endif /* COSMOSIM_API_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/cosmosim_api.h
git commit -m "feat: add cosmosim C API header for shared library"
```

---

## Task 2: C API Implementation (`cosmosim_api.c`)

**Files:**
- Create: `src/cosmosim_api.c`
- Reference: `src/main.c:374-418` (allocation + init pattern), `src/octree.h`, `src/integrator.h`, `src/sph.h`, `src/quasar.h`, `src/initial_conditions.h`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_physics.c`, before `main()`:

```c
int test_api_create_destroy(void) {
    CosmosimConfig cfg = cosmosim_default_config();
    cfg.n_bodies = 1000;
    cfg.merger = 0;
    cfg.quasar = 0;
    SimHandle h = cosmosim_create(cfg);
    ASSERT(h != NULL, "cosmosim_create returned NULL");

    const Body *bodies = cosmosim_get_bodies(h);
    ASSERT(bodies != NULL, "cosmosim_get_bodies returned NULL");

    int count = cosmosim_get_count(h);
    ASSERT(count == 1000, "count should be 1000");

    int active = cosmosim_get_active_count(h);
    ASSERT(active == 1000, "all 1000 bodies should be active initially");

    double t = cosmosim_get_sim_time(h);
    ASSERT_NEAR(t, 0.0, 1e-12, "sim time should start at 0");

    cosmosim_destroy(h);
    return 1;
}
```

Add to `main()` in the test file, alongside the other `RUN_TEST` calls:

```c
RUN_TEST(test_api_create_destroy);
```

Add the include at the top of `tests/test_physics.c`:

```c
#include "cosmosim_api.h"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/test_physics 2>&1 | tail -5`
Expected: Linker error — `cosmosim_create` undefined.

- [ ] **Step 3: Implement the API**

Create `src/cosmosim_api.c`:

```c
#include "cosmosim_api.h"
#include "octree.h"
#include "integrator.h"
#include "initial_conditions.h"
#include "quasar.h"
#include "sph.h"
#include <stdlib.h>
#include <string.h>

#define G        1.0
#define SOFTENING 0.5

typedef struct {
    Body *bodies;
    OctreeNode *pool;
    int n_alloc;
    int current_n;
    CosmosimConfig config;
    QuasarConfig qcfg;
    double sim_time;
    int compact_counter;
} SimState;

CosmosimConfig cosmosim_default_config(void) {
    CosmosimConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.n_bodies = 20000;
    cfg.merger = 0;
    cfg.quasar = 0;
    cfg.dt = 0.005;
    cfg.theta = 0.5;
    cfg.smbh_mass_frac = 0.05;
    cfg.accretion_radius = 6.0;
    cfg.jet_speed = 15.0;
    cfg.feedback_strength = 0.3;
    cfg.substeps = 2;
    return cfg;
}

SimHandle cosmosim_create(CosmosimConfig config) {
    int n = config.n_bodies;
    int n_dust = (config.merger && config.quasar) ? n / 5 : 0;
    int n_alloc = config.quasar ? n + n_dust + n / 4 : n;

    SimState *state = calloc(1, sizeof(SimState));
    if (!state) return NULL;

    state->bodies = calloc(n_alloc, sizeof(Body));
    state->pool = malloc(8 * n_alloc * sizeof(OctreeNode));
    if (!state->bodies || !state->pool) {
        free(state->bodies);
        free(state->pool);
        free(state);
        return NULL;
    }

    state->n_alloc = n_alloc;
    state->config = config;
    state->sim_time = 0.0;
    state->compact_counter = 0;

    /* Generate initial conditions */
    double galaxy_mass = 100.0;
    double disk_radius = 15.0;

    if (config.merger && config.quasar) {
        generate_quasar_merger(state->bodies, n, 40.0, 0.15, config.smbh_mass_frac);
        generate_merger_dust(state->bodies, n, n_dust, 40.0);
        state->current_n = n + n_dust;
    } else if (config.merger) {
        generate_merger(state->bodies, n, 40.0, 0.15);
        state->current_n = n;
    } else if (config.quasar) {
        generate_quasar_galaxy(state->bodies, n, 0.0, 0.0,
                               galaxy_mass, disk_radius, 0.0, 0.0,
                               config.smbh_mass_frac);
        state->current_n = n;
    } else {
        generate_spiral_galaxy(state->bodies, n, 0.0, 0.0,
                               galaxy_mass, disk_radius, 0.0, 0.0);
        state->current_n = n;
    }

    /* Initialize accelerations */
    integrator_init_accelerations(state->bodies, state->current_n,
                                  G, SOFTENING, config.theta, state->pool);

    /* Quasar config */
    state->qcfg = quasar_default_config();
    if (config.quasar) {
        state->qcfg.accretion_radius = config.accretion_radius;
        state->qcfg.jet_speed = config.jet_speed;
        state->qcfg.feedback_strength = config.feedback_strength;
        state->qcfg.jet_cap = n / 10;
        state->qcfg.max_bodies = n_alloc;
    }

    return (SimHandle)state;
}

void cosmosim_step(SimHandle handle) {
    SimState *s = (SimState*)handle;
    int sph_enabled = 1;

    for (int sub = 0; sub < s->config.substeps; sub++) {
        integrator_step(s->bodies, s->current_n, s->config.dt,
                        G, SOFTENING, s->config.theta, s->pool, sph_enabled);
        if (s->config.quasar) {
            quasar_step(s->bodies, &s->current_n, s->n_alloc,
                        &s->qcfg, s->config.dt);
        }
    }

    if (s->config.quasar) {
        s->compact_counter++;
        if (s->compact_counter >= 60) {
            s->current_n = quasar_compact(s->bodies, s->current_n);
            s->compact_counter = 0;
        }
    }

    s->sim_time += s->config.dt * s->config.substeps;
}

const Body* cosmosim_get_bodies(SimHandle handle) {
    SimState *s = (SimState*)handle;
    return s->bodies;
}

int cosmosim_get_count(SimHandle handle) {
    SimState *s = (SimState*)handle;
    return s->current_n;
}

int cosmosim_get_active_count(SimHandle handle) {
    SimState *s = (SimState*)handle;
    int active = 0;
    for (int i = 0; i < s->current_n; i++) {
        if (s->bodies[i].mass > 0.0) active++;
    }
    return active;
}

double cosmosim_get_sim_time(SimHandle handle) {
    SimState *s = (SimState*)handle;
    return s->sim_time;
}

void cosmosim_destroy(SimHandle handle) {
    if (!handle) return;
    SimState *s = (SimState*)handle;
    free(s->bodies);
    free(s->pool);
    free(s);
}
```

- [ ] **Step 4: Add `cosmosim_api.c` to the `cosmosim_physics` static library in CMakeLists.txt**

In `CMakeLists.txt`, add `src/cosmosim_api.c` to the `cosmosim_physics` source list:

Find the line:
```cmake
add_library(cosmosim_physics STATIC src/octree.c src/integrator.c src/initial_conditions.c src/quasar.c src/sph.c)
```

Change to:
```cmake
add_library(cosmosim_physics STATIC src/octree.c src/integrator.c src/initial_conditions.c src/quasar.c src/sph.c src/cosmosim_api.c)
```

- [ ] **Step 5: Build and run tests**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/test_physics 2>&1 | tail -5`
Expected: `test_api_create_destroy: PASS` and all other tests still pass.

- [ ] **Step 6: Add step test**

Add to `tests/test_physics.c`, before `main()`:

```c
int test_api_step(void) {
    CosmosimConfig cfg = cosmosim_default_config();
    cfg.n_bodies = 500;
    cfg.merger = 0;
    cfg.quasar = 0;
    cfg.substeps = 1;
    SimHandle h = cosmosim_create(cfg);

    cosmosim_step(h);

    double t = cosmosim_get_sim_time(h);
    ASSERT_NEAR(t, cfg.dt, 1e-12, "sim time should advance by dt after one step");

    /* Bodies should have moved */
    const Body *bodies = cosmosim_get_bodies(h);
    double total_v2 = 0.0;
    int count = cosmosim_get_count(h);
    for (int i = 0; i < count; i++) {
        total_v2 += bodies[i].vx * bodies[i].vx +
                    bodies[i].vy * bodies[i].vy +
                    bodies[i].vz * bodies[i].vz;
    }
    ASSERT(total_v2 > 0.0, "bodies should have nonzero velocities after step");

    cosmosim_destroy(h);
    return 1;
}
```

Add to `main()`:

```c
RUN_TEST(test_api_step);
```

- [ ] **Step 7: Run tests**

Run: `cmake --build build && ./build/test_physics 2>&1 | tail -5`
Expected: Both API tests pass, all other tests pass.

- [ ] **Step 8: Add quasar mode test**

Add to `tests/test_physics.c`, before `main()`:

```c
int test_api_quasar_mode(void) {
    CosmosimConfig cfg = cosmosim_default_config();
    cfg.n_bodies = 1000;
    cfg.merger = 1;
    cfg.quasar = 1;
    cfg.substeps = 1;
    SimHandle h = cosmosim_create(cfg);

    /* Should have SMBH bodies */
    const Body *bodies = cosmosim_get_bodies(h);
    int count = cosmosim_get_count(h);
    int has_smbh = 0;
    for (int i = 0; i < count; i++) {
        if (bodies[i].type == BODY_SMBH) { has_smbh = 1; break; }
    }
    ASSERT(has_smbh, "quasar merger should have at least one SMBH");

    /* Step should not crash */
    for (int i = 0; i < 5; i++) {
        cosmosim_step(h);
    }
    ASSERT(cosmosim_get_active_count(h) > 0, "should have active bodies after stepping");

    cosmosim_destroy(h);
    return 1;
}
```

Add to `main()`:

```c
RUN_TEST(test_api_quasar_mode);
```

- [ ] **Step 9: Run tests**

Run: `cmake --build build && ./build/test_physics 2>&1 | tail -5`
Expected: All tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/cosmosim_api.c src/cosmosim_api.h CMakeLists.txt tests/test_physics.c
git commit -m "feat: implement cosmosim C API layer with tests"
```

---

## Task 3: Shared Library CMake Target

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add shared library target**

Add after the existing `cosmosim_physics` static library block in `CMakeLists.txt`:

```cmake
# Shared library for UE plugin (physics only, no renderer/OpenGL)
add_library(cosmosim_shared SHARED
    src/octree.c src/integrator.c src/initial_conditions.c
    src/quasar.c src/sph.c src/cosmosim_api.c
)
target_include_directories(cosmosim_shared PUBLIC src/)
target_compile_definitions(cosmosim_shared PRIVATE COSMOSIM_BUILDING_DLL)
set_target_properties(cosmosim_shared PROPERTIES
    OUTPUT_NAME cosmosim
    C_VISIBILITY_PRESET hidden
)
if(OpenMP_C_FOUND)
    target_link_libraries(cosmosim_shared PRIVATE OpenMP::OpenMP_C)
endif()
target_link_libraries(cosmosim_shared PRIVATE m)
```

- [ ] **Step 2: Build and verify shared library is produced**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ls build/libcosmosim.*`
Expected: `build/libcosmosim.dylib` (macOS) exists. The existing `build/cosmosim` executable and `build/test_physics` should also still build.

- [ ] **Step 3: Verify existing tests still pass**

Run: `./build/test_physics 2>&1 | tail -3`
Expected: All tests pass (no regressions from adding the shared target).

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add cosmosim_shared library target for UE plugin"
```

---

## Task 4: UE Plugin Scaffolding

**Files:**
- Create: `ue/CosmosimPlugin/CosmosimPlugin.uplugin`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/CosmosimPlugin.Build.cs`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimModule.h`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimModule.cpp`

- [ ] **Step 1: Create plugin descriptor**

Create `ue/CosmosimPlugin/CosmosimPlugin.uplugin`:

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "FriendlyName": "Cosmosim",
    "Description": "N-body gravity simulation renderer using Niagara",
    "Category": "Simulation",
    "CreatedBy": "cosmosim",
    "EnabledByDefault": true,
    "CanContainContent": true,
    "Modules": [
        {
            "Name": "CosmosimPlugin",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }
    ],
    "Plugins": [
        {
            "Name": "Niagara",
            "Enabled": true
        }
    ]
}
```

- [ ] **Step 2: Create Build.cs**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/CosmosimPlugin.Build.cs`:

```csharp
using UnrealBuildTool;
using System.IO;

public class CosmosimPlugin : ModuleRules
{
    public CosmosimPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Niagara",
            "NiagaraCore",
            "NiagaraShader",
            "RenderCore",
            "RHI",
            "EnhancedInput",
            "UMG",
            "Slate",
            "SlateCore"
        });

        // Link libcosmosim
        string ThirdPartyPath = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "libcosmosim");
        string IncludePath = Path.Combine(ThirdPartyPath, "include");
        string LibPath = Path.Combine(ThirdPartyPath, "lib");

        PublicIncludePaths.Add(IncludePath);

        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcosmosim.dylib"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "libcosmosim.dylib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "cosmosim.lib"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "cosmosim.dll"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcosmosim.so"));
            RuntimeDependencies.Add(Path.Combine(LibPath, "libcosmosim.so"));
        }
    }
}
```

- [ ] **Step 3: Create module header**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimModule.h`:

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FCosmosimModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void* LibHandle = nullptr;
};
```

- [ ] **Step 4: Create module implementation**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimModule.cpp`:

```cpp
#include "CosmosimModule.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FCosmosimModule"

void FCosmosimModule::StartupModule()
{
    FString LibDir = FPaths::Combine(
        IPluginManager::Get().FindPlugin(TEXT("CosmosimPlugin"))->GetBaseDir(),
        TEXT("ThirdParty/libcosmosim/lib"));

#if PLATFORM_MAC
    FString LibName = TEXT("libcosmosim.dylib");
#elif PLATFORM_WINDOWS
    FString LibName = TEXT("cosmosim.dll");
#elif PLATFORM_LINUX
    FString LibName = TEXT("libcosmosim.so");
#endif

    FString LibPath = FPaths::Combine(LibDir, LibName);
    LibHandle = FPlatformProcess::GetDllHandle(*LibPath);

    if (!LibHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("Cosmosim: Failed to load %s"), *LibPath);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Cosmosim: Loaded %s"), *LibPath);
    }
}

void FCosmosimModule::ShutdownModule()
{
    if (LibHandle)
    {
        FPlatformProcess::FreeDllHandle(LibHandle);
        LibHandle = nullptr;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCosmosimModule, CosmosimPlugin)
```

- [ ] **Step 5: Create ThirdParty directory structure and copy headers**

```bash
mkdir -p ue/CosmosimPlugin/ThirdParty/libcosmosim/include
mkdir -p ue/CosmosimPlugin/ThirdParty/libcosmosim/lib
cp src/cosmosim_api.h ue/CosmosimPlugin/ThirdParty/libcosmosim/include/
cp src/body.h ue/CosmosimPlugin/ThirdParty/libcosmosim/include/
```

- [ ] **Step 6: Commit**

```bash
git add ue/
git commit -m "feat: scaffold UE 5.4 plugin with module and libcosmosim linkage"
```

---

## Task 5: Cosmosim Subsystem (Physics Thread + Double Buffer)

**Files:**
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimSubsystem.h`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimSubsystem.cpp`

- [ ] **Step 1: Create subsystem header**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimSubsystem.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include <atomic>
#include "CosmosimSubsystem.generated.h"

// Forward declare C types
extern "C" {
    #include "cosmosim_api.h"
}

/**
 * Packed float representation of a Body for GPU upload.
 * 16 floats = 64 bytes (GPU-aligned).
 */
struct FCosmosimBodyGPU
{
    float PosX, PosY, PosZ;
    float Mass;
    float VelX, VelY, VelZ;
    float Type;
    float Temperature;  // internal_energy
    float Density;
    float Lifetime;
    float Luminosity;
    float SpinX, SpinY, SpinZ;
    float _Pad;         // pad to 64 bytes
};

/**
 * Physics thread runnable.
 */
class FCosmosimPhysicsRunnable : public FRunnable
{
public:
    FCosmosimPhysicsRunnable(SimHandle InHandle, FCosmosimBodyGPU* BufferA,
                             FCosmosimBodyGPU* BufferB, int MaxBodies);

    virtual uint32 Run() override;
    virtual void Stop() override;

    std::atomic<int> WriteIndex{0};
    std::atomic<int> ActiveCount{0};
    std::atomic<double> SimTime{0.0};
    std::atomic<bool> Paused{false};

private:
    SimHandle Handle;
    FCosmosimBodyGPU* Buffers[2];
    int MaxBodies;
    std::atomic<bool> Running{true};

    void SnapshotToBuffer(FCosmosimBodyGPU* Dest);
};

UCLASS()
class COSMOSIMPLUGIN_API UCosmosimSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Get the current read buffer (safe to read from game/render thread). */
    const FCosmosimBodyGPU* GetReadBuffer() const;

    /** Number of active (mass > 0) bodies in the current snapshot. */
    int GetActiveCount() const;

    /** Current simulation time. */
    double GetSimTime() const;

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    void PauseSim();

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    void ResumeSim();

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    bool IsPaused() const;

    /** Configuration — set before Initialize or call ResetSim. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    int NumBodies = 20000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    bool MergerMode = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    bool QuasarMode = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    float Timestep = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    float Theta = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmosim")
    int Substeps = 2;

private:
    SimHandle SimHandlePtr = nullptr;
    FCosmosimPhysicsRunnable* PhysicsRunnable = nullptr;
    FRunnableThread* PhysicsThread = nullptr;
    FCosmosimBodyGPU* BufferA = nullptr;
    FCosmosimBodyGPU* BufferB = nullptr;
    int AllocatedCount = 0;
};
```

- [ ] **Step 2: Create subsystem implementation**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimSubsystem.cpp`:

```cpp
#include "CosmosimSubsystem.h"

// --- FCosmosimPhysicsRunnable ---

FCosmosimPhysicsRunnable::FCosmosimPhysicsRunnable(
    SimHandle InHandle, FCosmosimBodyGPU* InBufferA,
    FCosmosimBodyGPU* InBufferB, int InMaxBodies)
    : Handle(InHandle), MaxBodies(InMaxBodies)
{
    Buffers[0] = InBufferA;
    Buffers[1] = InBufferB;
}

void FCosmosimPhysicsRunnable::SnapshotToBuffer(FCosmosimBodyGPU* Dest)
{
    const Body* Bodies = cosmosim_get_bodies(Handle);
    int Count = cosmosim_get_count(Handle);
    int Active = 0;

    for (int i = 0; i < Count && Active < MaxBodies; i++)
    {
        if (Bodies[i].mass <= 0.0) continue;

        FCosmosimBodyGPU& G = Dest[Active];
        G.PosX = (float)Bodies[i].x;
        G.PosY = (float)Bodies[i].y;
        G.PosZ = (float)Bodies[i].z;
        G.Mass = (float)Bodies[i].mass;
        G.VelX = (float)Bodies[i].vx;
        G.VelY = (float)Bodies[i].vy;
        G.VelZ = (float)Bodies[i].vz;
        G.Type = (float)Bodies[i].type;
        G.Temperature = (float)Bodies[i].internal_energy;
        G.Density = (float)Bodies[i].density;
        G.Lifetime = (float)Bodies[i].lifetime;
        G.Luminosity = (float)Bodies[i].luminosity;
        G.SpinX = (float)Bodies[i].spin_x;
        G.SpinY = (float)Bodies[i].spin_y;
        G.SpinZ = (float)Bodies[i].spin_z;
        G._Pad = 0.0f;
        Active++;
    }

    ActiveCount.store(Active, std::memory_order_release);
}

uint32 FCosmosimPhysicsRunnable::Run()
{
    // Initial snapshot
    SnapshotToBuffer(Buffers[WriteIndex.load()]);
    WriteIndex.store(WriteIndex.load() ^ 1, std::memory_order_release);

    while (Running.load(std::memory_order_acquire))
    {
        if (Paused.load(std::memory_order_acquire))
        {
            FPlatformProcess::Sleep(0.016f);
            continue;
        }

        cosmosim_step(Handle);
        SimTime.store(cosmosim_get_sim_time(Handle), std::memory_order_release);

        int WriteIdx = WriteIndex.load(std::memory_order_acquire);
        SnapshotToBuffer(Buffers[WriteIdx]);
        WriteIndex.store(WriteIdx ^ 1, std::memory_order_release);
    }

    return 0;
}

void FCosmosimPhysicsRunnable::Stop()
{
    Running.store(false, std::memory_order_release);
}

// --- UCosmosimSubsystem ---

void UCosmosimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    CosmosimConfig Cfg = cosmosim_default_config();
    Cfg.n_bodies = NumBodies;
    Cfg.merger = MergerMode ? 1 : 0;
    Cfg.quasar = QuasarMode ? 1 : 0;
    Cfg.dt = (double)Timestep;
    Cfg.theta = (double)Theta;
    Cfg.substeps = Substeps;

    SimHandlePtr = cosmosim_create(Cfg);
    if (!SimHandlePtr)
    {
        UE_LOG(LogTemp, Error, TEXT("Cosmosim: Failed to create simulation"));
        return;
    }

    AllocatedCount = cosmosim_get_count(SimHandlePtr);
    // Over-allocate for jet spawning headroom
    int BufferSize = AllocatedCount + AllocatedCount / 4;
    BufferA = new FCosmosimBodyGPU[BufferSize]();
    BufferB = new FCosmosimBodyGPU[BufferSize]();

    PhysicsRunnable = new FCosmosimPhysicsRunnable(
        SimHandlePtr, BufferA, BufferB, BufferSize);
    PhysicsThread = FRunnableThread::Create(
        PhysicsRunnable, TEXT("CosmosimPhysics"), 0, TPri_AboveNormal);

    UE_LOG(LogTemp, Log, TEXT("Cosmosim: Simulation started with %d bodies"), NumBodies);
}

void UCosmosimSubsystem::Deinitialize()
{
    if (PhysicsRunnable)
    {
        PhysicsRunnable->Stop();
    }
    if (PhysicsThread)
    {
        PhysicsThread->WaitForCompletion();
        delete PhysicsThread;
        PhysicsThread = nullptr;
    }
    delete PhysicsRunnable;
    PhysicsRunnable = nullptr;

    if (SimHandlePtr)
    {
        cosmosim_destroy(SimHandlePtr);
        SimHandlePtr = nullptr;
    }

    delete[] BufferA;
    delete[] BufferB;
    BufferA = nullptr;
    BufferB = nullptr;

    Super::Deinitialize();
}

const FCosmosimBodyGPU* UCosmosimSubsystem::GetReadBuffer() const
{
    if (!PhysicsRunnable) return nullptr;
    // Read from the buffer the physics thread is NOT writing to
    int ReadIdx = PhysicsRunnable->WriteIndex.load(std::memory_order_acquire) ^ 1;
    return (ReadIdx == 0) ? BufferA : BufferB;
}

int UCosmosimSubsystem::GetActiveCount() const
{
    if (!PhysicsRunnable) return 0;
    return PhysicsRunnable->ActiveCount.load(std::memory_order_acquire);
}

double UCosmosimSubsystem::GetSimTime() const
{
    if (!PhysicsRunnable) return 0.0;
    return PhysicsRunnable->SimTime.load(std::memory_order_acquire);
}

void UCosmosimSubsystem::PauseSim()
{
    if (PhysicsRunnable) PhysicsRunnable->Paused.store(true, std::memory_order_release);
}

void UCosmosimSubsystem::ResumeSim()
{
    if (PhysicsRunnable) PhysicsRunnable->Paused.store(false, std::memory_order_release);
}

bool UCosmosimSubsystem::IsPaused() const
{
    if (!PhysicsRunnable) return true;
    return PhysicsRunnable->Paused.load(std::memory_order_acquire);
}
```

- [ ] **Step 3: Commit**

```bash
git add ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimSubsystem.h \
        ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimSubsystem.cpp
git commit -m "feat: add CosmosimSubsystem with physics thread and double buffer"
```

---

## Task 6: Custom Niagara Data Interface

**Files:**
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/NiagaraDI_Cosmosim.h`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/NiagaraDI_Cosmosim.cpp`

- [ ] **Step 1: Create NDI header**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/NiagaraDI_Cosmosim.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "CosmosimSubsystem.h"
#include "NiagaraDI_Cosmosim.generated.h"

UCLASS(EditInlineNew, Category = "Cosmosim", meta = (DisplayName = "Cosmosim Particles"))
class COSMOSIMPLUGIN_API UNiagaraDI_Cosmosim : public UNiagaraDataInterface
{
    GENERATED_BODY()

public:
    UNiagaraDI_Cosmosim();

    // UNiagaraDataInterface overrides
    virtual void GetFunctions(
        TArray<FNiagaraFunctionSignature>& OutFunctions) override;
    virtual void GetVMExternalFunction(
        const FVMExternalFunctionBindingInfo& BindingInfo,
        void* InstanceData,
        FVMExternalFunction& OutFunc) override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override
    {
        return Target == ENiagaraSimTarget::CPUSim;
    }
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;
    virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

    // VM functions
    void GetNumBodies(FVectorVMExternalFunctionContext& Context);
    void GetBodyPosition(FVectorVMExternalFunctionContext& Context);
    void GetBodyVelocity(FVectorVMExternalFunctionContext& Context);
    void GetBodyMass(FVectorVMExternalFunctionContext& Context);
    void GetBodyType(FVectorVMExternalFunctionContext& Context);
    void GetBodyTemperature(FVectorVMExternalFunctionContext& Context);
    void GetBodyDensity(FVectorVMExternalFunctionContext& Context);
    void GetBodyLifetime(FVectorVMExternalFunctionContext& Context);
    void GetBodyLuminosity(FVectorVMExternalFunctionContext& Context);
    void GetBodySpinAxis(FVectorVMExternalFunctionContext& Context);

private:
    const FCosmosimBodyGPU* GetCurrentBuffer() const;
    int GetCurrentActiveCount() const;
};
```

- [ ] **Step 2: Create NDI implementation**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/NiagaraDI_Cosmosim.cpp`:

```cpp
#include "NiagaraDI_Cosmosim.h"
#include "NiagaraTypes.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

#define COSMOSIM_FUNC(Name) \
    { \
        FNiagaraFunctionSignature Sig; \
        Sig.Name = FName(TEXT(#Name)); \

static const FName GetNumBodiesName(TEXT("GetNumBodies"));
static const FName GetBodyPositionName(TEXT("GetBodyPosition"));
static const FName GetBodyVelocityName(TEXT("GetBodyVelocity"));
static const FName GetBodyMassName(TEXT("GetBodyMass"));
static const FName GetBodyTypeName(TEXT("GetBodyType"));
static const FName GetBodyTemperatureName(TEXT("GetBodyTemperature"));
static const FName GetBodyDensityName(TEXT("GetBodyDensity"));
static const FName GetBodyLifetimeName(TEXT("GetBodyLifetime"));
static const FName GetBodyLuminosityName(TEXT("GetBodyLuminosity"));
static const FName GetBodySpinAxisName(TEXT("GetBodySpinAxis"));

UNiagaraDI_Cosmosim::UNiagaraDI_Cosmosim()
{
    Proxy.Reset(new FNiagaraDataInterfaceProxy());
}

void UNiagaraDI_Cosmosim::GetFunctions(
    TArray<FNiagaraFunctionSignature>& OutFunctions)
{
    // GetNumBodies() -> int
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetNumBodiesName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("NumBodies")));
        OutFunctions.Add(Sig);
    }

    // GetBodyPosition(int Index) -> vec3
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyPositionName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
        OutFunctions.Add(Sig);
    }

    // GetBodyVelocity(int Index) -> vec3
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyVelocityName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
        OutFunctions.Add(Sig);
    }

    // GetBodyMass(int Index) -> float
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyMassName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetFloatDef(), TEXT("Mass")));
        OutFunctions.Add(Sig);
    }

    // GetBodyType(int Index) -> int
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyTypeName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Type")));
        OutFunctions.Add(Sig);
    }

    // GetBodyTemperature(int Index) -> float
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyTemperatureName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetFloatDef(), TEXT("Temperature")));
        OutFunctions.Add(Sig);
    }

    // GetBodyDensity(int Index) -> float
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyDensityName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetFloatDef(), TEXT("Density")));
        OutFunctions.Add(Sig);
    }

    // GetBodyLifetime(int Index) -> float
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyLifetimeName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetFloatDef(), TEXT("Lifetime")));
        OutFunctions.Add(Sig);
    }

    // GetBodyLuminosity(int Index) -> float
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodyLuminosityName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetFloatDef(), TEXT("Luminosity")));
        OutFunctions.Add(Sig);
    }

    // GetBodySpinAxis(int Index) -> vec3
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetBodySpinAxisName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetVec3Def(), TEXT("SpinAxis")));
        OutFunctions.Add(Sig);
    }
}

void UNiagaraDI_Cosmosim::GetVMExternalFunction(
    const FVMExternalFunctionBindingInfo& BindingInfo,
    void* InstanceData,
    FVMExternalFunction& OutFunc)
{
    if (BindingInfo.Name == GetNumBodiesName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetNumBodies);
    else if (BindingInfo.Name == GetBodyPositionName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyPosition);
    else if (BindingInfo.Name == GetBodyVelocityName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyVelocity);
    else if (BindingInfo.Name == GetBodyMassName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyMass);
    else if (BindingInfo.Name == GetBodyTypeName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyType);
    else if (BindingInfo.Name == GetBodyTemperatureName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyTemperature);
    else if (BindingInfo.Name == GetBodyDensityName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyDensity);
    else if (BindingInfo.Name == GetBodyLifetimeName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyLifetime);
    else if (BindingInfo.Name == GetBodyLuminosityName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodyLuminosity);
    else if (BindingInfo.Name == GetBodySpinAxisName)
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDI_Cosmosim::GetBodySpinAxis);
}

const FCosmosimBodyGPU* UNiagaraDI_Cosmosim::GetCurrentBuffer() const
{
    UWorld* World = GEngine->GetWorldContexts()[0].World();
    if (!World) return nullptr;

    UGameInstance* GI = World->GetGameInstance();
    if (!GI) return nullptr;

    UCosmosimSubsystem* Sub = GI->GetSubsystem<UCosmosimSubsystem>();
    if (!Sub) return nullptr;

    return Sub->GetReadBuffer();
}

int UNiagaraDI_Cosmosim::GetCurrentActiveCount() const
{
    UWorld* World = GEngine->GetWorldContexts()[0].World();
    if (!World) return 0;

    UGameInstance* GI = World->GetGameInstance();
    if (!GI) return 0;

    UCosmosimSubsystem* Sub = GI->GetSubsystem<UCosmosimSubsystem>();
    if (!Sub) return 0;

    return Sub->GetActiveCount();
}

// --- VM Function Implementations ---

void UNiagaraDI_Cosmosim::GetNumBodies(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncRegisterHandler<int32> OutNum(Context);
    int Count = GetCurrentActiveCount();
    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        *OutNum.GetDestAndAdvance() = Count;
    }
}

void UNiagaraDI_Cosmosim::GetBodyPosition(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
        {
            *OutX.GetDestAndAdvance() = Buf[Idx].PosX;
            *OutY.GetDestAndAdvance() = Buf[Idx].PosY;
            *OutZ.GetDestAndAdvance() = Buf[Idx].PosZ;
        }
        else
        {
            *OutX.GetDestAndAdvance() = 0.0f;
            *OutY.GetDestAndAdvance() = 0.0f;
            *OutZ.GetDestAndAdvance() = 0.0f;
        }
    }
}

void UNiagaraDI_Cosmosim::GetBodyVelocity(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
        {
            *OutX.GetDestAndAdvance() = Buf[Idx].VelX;
            *OutY.GetDestAndAdvance() = Buf[Idx].VelY;
            *OutZ.GetDestAndAdvance() = Buf[Idx].VelZ;
        }
        else
        {
            *OutX.GetDestAndAdvance() = 0.0f;
            *OutY.GetDestAndAdvance() = 0.0f;
            *OutZ.GetDestAndAdvance() = 0.0f;
        }
    }
}

void UNiagaraDI_Cosmosim::GetBodyMass(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutMass(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutMass.GetDestAndAdvance() = Buf[Idx].Mass;
        else
            *OutMass.GetDestAndAdvance() = 0.0f;
    }
}

void UNiagaraDI_Cosmosim::GetBodyType(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<int32> OutType(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutType.GetDestAndAdvance() = (int32)Buf[Idx].Type;
        else
            *OutType.GetDestAndAdvance() = 0;
    }
}

void UNiagaraDI_Cosmosim::GetBodyTemperature(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutTemp(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutTemp.GetDestAndAdvance() = Buf[Idx].Temperature;
        else
            *OutTemp.GetDestAndAdvance() = 0.0f;
    }
}

void UNiagaraDI_Cosmosim::GetBodyDensity(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutDensity(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutDensity.GetDestAndAdvance() = Buf[Idx].Density;
        else
            *OutDensity.GetDestAndAdvance() = 0.0f;
    }
}

void UNiagaraDI_Cosmosim::GetBodyLifetime(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutLifetime(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutLifetime.GetDestAndAdvance() = Buf[Idx].Lifetime;
        else
            *OutLifetime.GetDestAndAdvance() = 0.0f;
    }
}

void UNiagaraDI_Cosmosim::GetBodyLuminosity(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutLum(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
            *OutLum.GetDestAndAdvance() = Buf[Idx].Luminosity;
        else
            *OutLum.GetDestAndAdvance() = 0.0f;
    }
}

void UNiagaraDI_Cosmosim::GetBodySpinAxis(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
    VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);

    const FCosmosimBodyGPU* Buf = GetCurrentBuffer();
    int Count = GetCurrentActiveCount();

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        int32 Idx = InIndex.GetAndAdvance();
        if (Buf && Idx >= 0 && Idx < Count)
        {
            *OutX.GetDestAndAdvance() = Buf[Idx].SpinX;
            *OutY.GetDestAndAdvance() = Buf[Idx].SpinY;
            *OutZ.GetDestAndAdvance() = Buf[Idx].SpinZ;
        }
        else
        {
            *OutX.GetDestAndAdvance() = 0.0f;
            *OutY.GetDestAndAdvance() = 0.0f;
            *OutZ.GetDestAndAdvance() = 1.0f;
        }
    }
}

bool UNiagaraDI_Cosmosim::Equals(const UNiagaraDataInterface* Other) const
{
    return Super::Equals(Other) && CastChecked<UNiagaraDI_Cosmosim>(Other) != nullptr;
}

bool UNiagaraDI_Cosmosim::CopyToInternal(UNiagaraDataInterface* Destination) const
{
    if (!Super::CopyToInternal(Destination)) return false;
    // No instance data to copy — all state lives in the subsystem
    return true;
}
```

- [ ] **Step 3: Commit**

```bash
git add ue/CosmosimPlugin/Source/CosmosimPlugin/Public/NiagaraDI_Cosmosim.h \
        ue/CosmosimPlugin/Source/CosmosimPlugin/Private/NiagaraDI_Cosmosim.cpp
git commit -m "feat: add custom Niagara Data Interface for cosmosim particles"
```

---

## Task 7: Camera Controller

**Files:**
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimController.h`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimController.cpp`

- [ ] **Step 1: Create controller header**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimController.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "CosmosimController.generated.h"

class UInputMappingContext;
class UInputAction;
class UCosmosimSubsystem;

UENUM(BlueprintType)
enum class ECosmosimCameraMode : uint8
{
    Orbit,
    FreeCam,
    Track
};

UCLASS()
class COSMOSIMPLUGIN_API ACosmosimController : public APlayerController
{
    GENERATED_BODY()

public:
    ACosmosimController();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupInputComponent() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    ECosmosimCameraMode CameraMode = ECosmosimCameraMode::Orbit;

    UPROPERTY(EditAnywhere, Category = "Camera|Orbit")
    float OrbitDistance = 30.0f;

    UPROPERTY(EditAnywhere, Category = "Camera|Orbit")
    float OrbitAzimuth = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Camera|Orbit")
    float OrbitElevation = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Camera|Orbit")
    float OrbitSensitivity = 0.003f;

    UPROPERTY(EditAnywhere, Category = "Camera|Orbit")
    float ZoomSpeed = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Camera|Free")
    float FreeCamSpeed = 20.0f;

    UPROPERTY(EditAnywhere, Category = "Camera|Track")
    float TrackSmoothing = 0.03f;

    UPROPERTY(EditAnywhere, Category = "Camera|Track")
    float TrackMinDistance = 12.0f;

    UPROPERTY(EditAnywhere, Category = "Camera|Track")
    float TrackMaxDistance = 40.0f;

protected:
    void OnLookAction(const FInputActionValue& Value);
    void OnMoveAction(const FInputActionValue& Value);
    void OnZoomAction(const FInputActionValue& Value);
    void OnToggleMode();
    void OnTogglePause();
    void OnResetCamera();

private:
    UPROPERTY()
    UInputMappingContext* InputMapping = nullptr;

    UPROPERTY()
    UInputAction* LookAction = nullptr;

    UPROPERTY()
    UInputAction* MoveAction = nullptr;

    UPROPERTY()
    UInputAction* ZoomAction = nullptr;

    UPROPERTY()
    UInputAction* ToggleModeAction = nullptr;

    UPROPERTY()
    UInputAction* PauseAction = nullptr;

    UPROPERTY()
    UInputAction* ResetAction = nullptr;

    FVector OrbitTarget = FVector::ZeroVector;
    FVector SmoothedTarget = FVector::ZeroVector;
    FVector2D LookDelta = FVector2D::ZeroVector;
    FVector MoveDelta = FVector::ZeroVector;
    float ZoomDelta = 0.0f;

    void UpdateOrbitCamera(float DeltaTime);
    void UpdateFreeCam(float DeltaTime);
    void UpdateTrackCamera(float DeltaTime);

    FVector FindSMBHMidpoint() const;
};
```

- [ ] **Step 2: Create controller implementation**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimController.cpp`:

```cpp
#include "CosmosimController.h"
#include "CosmosimSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"

ACosmosimController::ACosmosimController()
{
    PrimaryActorTick.bCanEverTick = true;
    bShowMouseCursor = true;
}

void ACosmosimController::BeginPlay()
{
    Super::BeginPlay();

    // Create input actions programmatically
    InputMapping = NewObject<UInputMappingContext>(this);
    LookAction = NewObject<UInputAction>(this);
    LookAction->ValueType = EInputActionValueType::Axis2D;
    MoveAction = NewObject<UInputAction>(this);
    MoveAction->ValueType = EInputActionValueType::Axis3D;
    ZoomAction = NewObject<UInputAction>(this);
    ZoomAction->ValueType = EInputActionValueType::Axis1D;
    ToggleModeAction = NewObject<UInputAction>(this);
    PauseAction = NewObject<UInputAction>(this);
    ResetAction = NewObject<UInputAction>(this);

    if (UEnhancedInputLocalPlayerSubsystem* EIS =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        EIS->AddMappingContext(InputMapping, 0);
    }
}

void ACosmosimController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
    {
        EIC->BindAction(LookAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnLookAction);
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnMoveAction);
        EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnZoomAction);
        EIC->BindAction(ToggleModeAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnToggleMode);
        EIC->BindAction(PauseAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnTogglePause);
        EIC->BindAction(ResetAction, ETriggerEvent::Triggered, this,
                        &ACosmosimController::OnResetCamera);
    }
}

void ACosmosimController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    switch (CameraMode)
    {
    case ECosmosimCameraMode::Orbit:
        UpdateOrbitCamera(DeltaTime);
        break;
    case ECosmosimCameraMode::FreeCam:
        UpdateFreeCam(DeltaTime);
        break;
    case ECosmosimCameraMode::Track:
        UpdateTrackCamera(DeltaTime);
        break;
    }

    // Reset per-frame deltas
    LookDelta = FVector2D::ZeroVector;
    MoveDelta = FVector::ZeroVector;
    ZoomDelta = 0.0f;
}

void ACosmosimController::OnLookAction(const FInputActionValue& Value)
{
    LookDelta = Value.Get<FVector2D>();
}

void ACosmosimController::OnMoveAction(const FInputActionValue& Value)
{
    MoveDelta = Value.Get<FVector>();
}

void ACosmosimController::OnZoomAction(const FInputActionValue& Value)
{
    ZoomDelta = Value.Get<float>();
}

void ACosmosimController::OnToggleMode()
{
    int Mode = (int)CameraMode;
    Mode = (Mode + 1) % 3;
    CameraMode = (ECosmosimCameraMode)Mode;
}

void ACosmosimController::OnTogglePause()
{
    UCosmosimSubsystem* Sub = GetGameInstance()->GetSubsystem<UCosmosimSubsystem>();
    if (!Sub) return;

    if (Sub->IsPaused())
        Sub->ResumeSim();
    else
        Sub->PauseSim();
}

void ACosmosimController::OnResetCamera()
{
    OrbitAzimuth = 0.0f;
    OrbitElevation = 0.3f;
    OrbitDistance = 30.0f;
    OrbitTarget = FVector::ZeroVector;
    SmoothedTarget = FVector::ZeroVector;
}

void ACosmosimController::UpdateOrbitCamera(float DeltaTime)
{
    // Apply look delta (left-drag orbits)
    OrbitAzimuth += LookDelta.X * OrbitSensitivity;
    OrbitElevation = FMath::Clamp(
        OrbitElevation + LookDelta.Y * OrbitSensitivity,
        -1.5f, 1.5f);

    // Apply zoom
    OrbitDistance = FMath::Clamp(OrbitDistance - ZoomDelta * ZoomSpeed, 5.0f, 200.0f);

    // Pan target (right-drag)
    OrbitTarget += GetPawn()->GetActorRightVector() * MoveDelta.X * 0.5f;
    OrbitTarget += GetPawn()->GetActorUpVector() * MoveDelta.Y * 0.5f;

    // Compute eye position from spherical coordinates
    float CosElev = FMath::Cos(OrbitElevation);
    FVector EyePos = OrbitTarget + FVector(
        OrbitDistance * CosElev * FMath::Cos(OrbitAzimuth),
        OrbitDistance * CosElev * FMath::Sin(OrbitAzimuth),
        OrbitDistance * FMath::Sin(OrbitElevation));

    // Apply
    if (APawn* P = GetPawn())
    {
        P->SetActorLocation(EyePos);
        P->SetActorRotation((OrbitTarget - EyePos).Rotation());
    }
}

void ACosmosimController::UpdateFreeCam(float DeltaTime)
{
    APawn* P = GetPawn();
    if (!P) return;

    FRotator Rot = P->GetActorRotation();
    Rot.Yaw += LookDelta.X * 0.2f;
    Rot.Pitch = FMath::Clamp(Rot.Pitch + LookDelta.Y * 0.2f, -89.0f, 89.0f);
    P->SetActorRotation(Rot);

    FVector Forward = Rot.Vector();
    FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
    FVector Up = FVector::UpVector;

    FVector Velocity = Forward * MoveDelta.X + Right * MoveDelta.Y + Up * MoveDelta.Z;
    P->SetActorLocation(P->GetActorLocation() + Velocity * FreeCamSpeed * DeltaTime);
}

void ACosmosimController::UpdateTrackCamera(float DeltaTime)
{
    FVector Target = FindSMBHMidpoint();

    // Exponential smoothing (matching current C implementation)
    SmoothedTarget = SmoothedTarget * (1.0f - TrackSmoothing) + Target * TrackSmoothing;

    // Adaptive distance based on SMBH separation
    // (For now use fixed orbit distance; with 2 SMBHs would compute separation)
    float Dist = FMath::Clamp(OrbitDistance, TrackMinDistance, TrackMaxDistance);

    // Apply look delta for manual orbit adjustment
    OrbitAzimuth += LookDelta.X * OrbitSensitivity;
    OrbitElevation = FMath::Clamp(
        OrbitElevation + LookDelta.Y * OrbitSensitivity,
        -1.5f, 1.5f);
    OrbitDistance = FMath::Clamp(OrbitDistance - ZoomDelta * ZoomSpeed,
                                TrackMinDistance, TrackMaxDistance);

    float CosElev = FMath::Cos(OrbitElevation);
    FVector EyePos = SmoothedTarget + FVector(
        Dist * CosElev * FMath::Cos(OrbitAzimuth),
        Dist * CosElev * FMath::Sin(OrbitAzimuth),
        Dist * FMath::Sin(OrbitElevation));

    if (APawn* P = GetPawn())
    {
        P->SetActorLocation(EyePos);
        P->SetActorRotation((SmoothedTarget - EyePos).Rotation());
    }
}

FVector ACosmosimController::FindSMBHMidpoint() const
{
    UCosmosimSubsystem* Sub = GetGameInstance()->GetSubsystem<UCosmosimSubsystem>();
    if (!Sub) return FVector::ZeroVector;

    const FCosmosimBodyGPU* Buf = Sub->GetReadBuffer();
    int Count = Sub->GetActiveCount();
    if (!Buf || Count == 0) return FVector::ZeroVector;

    FVector Sum = FVector::ZeroVector;
    int SMBHCount = 0;

    for (int i = 0; i < Count; i++)
    {
        if ((int)Buf[i].Type == 2) // BODY_SMBH
        {
            Sum += FVector(Buf[i].PosX, Buf[i].PosY, Buf[i].PosZ);
            SMBHCount++;
        }
    }

    if (SMBHCount > 0)
        return Sum / (float)SMBHCount;

    // Fallback: center of mass
    float TotalMass = 0.0f;
    FVector CoM = FVector::ZeroVector;
    for (int i = 0; i < Count; i++)
    {
        CoM += FVector(Buf[i].PosX, Buf[i].PosY, Buf[i].PosZ) * Buf[i].Mass;
        TotalMass += Buf[i].Mass;
    }
    return TotalMass > 0.0f ? CoM / TotalMass : FVector::ZeroVector;
}
```

- [ ] **Step 3: Commit**

```bash
git add ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimController.h \
        ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimController.cpp
git commit -m "feat: add CosmosimController with orbit, free, and track camera modes"
```

---

## Task 8: HUD Widget

**Files:**
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimHUD.h`
- Create: `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimHUD.cpp`

- [ ] **Step 1: Create HUD header**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimHUD.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CosmosimHUD.generated.h"

class UTextBlock;

UCLASS()
class COSMOSIMPLUGIN_API UCosmosimHUD : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* ParticleCountText = nullptr;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* SimTimeText = nullptr;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* PausedText = nullptr;
};
```

- [ ] **Step 2: Create HUD implementation**

Create `ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimHUD.cpp`:

```cpp
#include "CosmosimHUD.h"
#include "CosmosimSubsystem.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"

void UCosmosimHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    UGameInstance* GI = UGameplayStatics::GetGameInstance(GetWorld());
    if (!GI) return;

    UCosmosimSubsystem* Sub = GI->GetSubsystem<UCosmosimSubsystem>();
    if (!Sub) return;

    if (ParticleCountText)
    {
        ParticleCountText->SetText(FText::FromString(
            FString::Printf(TEXT("Particles: %d"), Sub->GetActiveCount())));
    }

    if (SimTimeText)
    {
        SimTimeText->SetText(FText::FromString(
            FString::Printf(TEXT("Time: %.2f"), Sub->GetSimTime())));
    }

    if (PausedText)
    {
        PausedText->SetVisibility(
            Sub->IsPaused() ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add ue/CosmosimPlugin/Source/CosmosimPlugin/Public/CosmosimHUD.h \
        ue/CosmosimPlugin/Source/CosmosimPlugin/Private/CosmosimHUD.cpp
git commit -m "feat: add CosmosimHUD widget for particle count, sim time, and pause state"
```

---

## Task 9: Niagara System Setup Guide (Editor Content)

This task documents the exact Niagara system configurations to create in the UE editor. These are `.uasset` files that cannot be created from code — they must be authored using the Niagara editor.

**Files:**
- Create: `ue/CosmosimPlugin/Content/README_NIAGARA_SETUP.md`

- [ ] **Step 1: Write Niagara setup guide**

Create `ue/CosmosimPlugin/Content/README_NIAGARA_SETUP.md`:

```markdown
# Niagara System Setup Guide

All systems use the `Cosmosim Particles` Data Interface (UNiagaraDI_Cosmosim).
Each system filters by body type using `GetBodyType()`.

## Common Setup (all systems)

1. Create new Niagara System → Empty System
2. Add Emitter → Empty Emitter
3. In Emitter Properties:
   - Sim Target: CPU
   - Deterministic: Off
   - Fixed Bounds: On (set to 200 units in all axes)
4. Add Data Interface: "Cosmosim Particles" to System User Parameters
5. Spawn module: use "Spawn Burst Instantaneous" or set spawn rate
   to match `GetNumBodies()` and filter by type
6. Particle Update:
   - Read index via `ExecutionIndex`
   - Call `GetBodyType(Index)` → if type != target, kill particle
   - Call `GetBodyPosition(Index)` → set `Particles.Position`
   - Call remaining attribute functions as needed per type

## NS_Stars (Type = 0)

- **Renderer:** Sprite Renderer
- **Material:** M_Star (Translucent, Emissive, Additive blend)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - SpriteSize ← `lerp(1, 8, GetBodyMass(Index) / MaxMass)`
  - Color ← velocity magnitude mapped through blue→white→yellow→orange curve
    - `speed = length(GetBodyVelocity(Index))`
    - Ramp: 0→blue(0.3,0.5,1), 0.3→white, 0.6→yellow(1,0.9,0.3), 1→orange(1,0.5,0.1)
  - Emissive ← `Mass * 2.0`
- **LOD:** Scale alpha by `1 - saturate((CameraDistance - 80) / 40)`

## NS_Gas (Type = 1)

- **Renderer:** Sprite Renderer (Translucent, no depth write)
- **Material:** M_Gas_Volumetric (Translucent, soft particle depth fade)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - SpriteSize ← `BaseSize / (GetBodyDensity(Index) + 0.1)`
    where BaseSize = 4.0
  - Color ← temperature ramp from `GetBodyTemperature(Index)`:
    - `t = saturate(log(Temperature + 1) / 3.0)`
    - t < 0.5: lerp(purple #331a66, orange #ff6619, t * 2)
    - t >= 0.5: lerp(orange #ff6619, blue-white #f8f8ff, (t - 0.5) * 2)
  - Alpha ← `clamp(Density / 0.5, 0.1, 0.8)`
  - Emissive ← `Temperature * 3.0`
- **Sub-UV:** 2x2 flipbook (4 frames), random start frame per particle

## NS_Jets (Type = 3)

- **Renderer:** Ribbon Renderer (primary) + Sprite Renderer (knots)
- **Material:** M_Jet_Ribbon (Translucent, Emissive, Additive)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - RibbonWidth ← `lerp(0.5, 2.0, GetBodyMass(Index) / KnotMass)`
  - Color ← relativistic Doppler:
    - `CamDir = normalize(CameraPos - Position)`
    - `beta = length(Velocity) / 15.0` (jet_speed normalization)
    - `cosTheta = dot(normalize(Velocity), CamDir)`
    - `D = 1.0 / (sqrt(1 - beta*beta) * (1 - beta * cosTheta))`
    - Approaching (D > 1): blue-shifted → color * (0.5, 0.7, 1.5) * D^3
    - Receding (D < 1): red-shifted → color * (1.5, 0.5, 0.3) * D^3
  - Alpha ← `GetBodyLifetime(Index) / MaxLifetime`
  - Emissive ← `Mass * 10.0`
- **Knots:** Particles with `Mass > KnotThreshold` get extra sprite overlay

## NS_SMBH (Type = 2)

- **Renderer:** Mesh Renderer (event horizon sphere) + Sprite (luminosity halo)
- **Material:** M_SMBH_EventHorizon (Unlit, black)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - MeshScale ← Schwarzschild radius: `Mass * 0.01` (tunable)
  - MeshRotation ← align with `GetBodySpinAxis(Index)`
  - HaloSize ← `GetBodyLuminosity(Index) * 5.0`
  - HaloEmissive ← `Luminosity * 20.0` (major bloom driver)

## NS_Lobes (Type = 5)

- **Renderer:** Sprite Renderer
- **Material:** M_Star (reuse, orange-red tint)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - SpriteSize ← `lerp(5, 15, Mass / MaxLobeMass)`
  - Color ← fixed orange-red #ff6633
  - Emissive ← `Mass * 8.0`

## NS_Dust (Type = 4)

- **Renderer:** Sprite Renderer
- **Material:** M_Star (reuse, brown tint, low alpha)
- **Particle Update modules:**
  - Position ← `GetBodyPosition(Index)`
  - SpriteSize ← `lerp(0.5, 2.0, Mass / MaxDustMass)`
  - Color ← fixed warm grey-brown #8b7355
  - Alpha ← `lerp(0.1, 0.3, Mass / MaxDustMass)`
  - Emissive ← `0.1` (minimal self-illumination)
```

- [ ] **Step 2: Commit**

```bash
git add ue/CosmosimPlugin/Content/README_NIAGARA_SETUP.md
git commit -m "docs: add Niagara system setup guide for all 6 body types"
```

---

## Task 10: Materials Setup Guide (Editor Content)

**Files:**
- Create: `ue/CosmosimPlugin/Content/README_MATERIALS.md`

- [ ] **Step 1: Write materials guide**

Create `ue/CosmosimPlugin/Content/README_MATERIALS.md`:

```markdown
# Materials Setup Guide

## M_Star

- **Blend Mode:** Additive
- **Shading Model:** Unlit
- **Material Domain:** Surface
- **Used with:** Niagara Sprites
- **Graph:**
  - Particle Color → multiply with gaussian falloff texture (radial gradient, white center → transparent edge)
  - Multiply by Emissive Strength parameter (default 2.0)
  - Output → Emissive Color
  - Radial gradient alpha → Opacity

## M_Gas_Volumetric

- **Blend Mode:** Translucent
- **Shading Model:** Unlit
- **Used with:** Niagara Sprites
- **Depth Fade:** Enabled (Fade Distance = 50 units) for soft particle effect
- **Graph:**
  - Particle Color → Emissive Color
  - Cloud noise texture (tiled, SubUV 2x2) → multiply with Particle Alpha
  - DepthFade node → multiply into opacity
  - Output → Opacity

## M_Jet_Ribbon

- **Blend Mode:** Additive
- **Shading Model:** Unlit
- **Used with:** Niagara Ribbons
- **Graph:**
  - Particle Color (contains Doppler-shifted color from Niagara) → Emissive Color
  - Ribbon UVs: V-axis gradient for edge fade (bright center, transparent edges)
  - Multiply by Emissive Strength parameter (default 5.0)

## M_SMBH_EventHorizon

- **Blend Mode:** Opaque
- **Shading Model:** Unlit
- **Graph:**
  - Constant black (0, 0, 0) → Base Color
  - Constant black → Emissive Color
  - This creates a fully light-absorbing sphere

## PP_GravLensing

- **Material Domain:** Post Process
- **Blendable Location:** After Tonemapping
- **Graph (HLSL Custom node):**

```hlsl
// Inputs: SceneTexture UV, SMBH screen positions/masses (material parameters)
float2 UV = GetDefaultSceneTextureUV(Parameters, 14); // SceneColor
float2 TotalOffset = float2(0, 0);

for (int s = 0; s < SMBHCount; s++)
{
    float2 Delta = UV - SMBHScreenPos[s];
    float Dist = length(Delta);
    float2 Dir = Delta / max(Dist, 0.001);

    // Einstein deflection
    float Deflection = LensingStrength[s] / max(Dist, 0.01);
    TotalOffset += Dir * Deflection;

    // Event horizon shadow
    if (Dist < SchwarzschildRadius[s])
    {
        float Edge = smoothstep(SchwarzschildRadius[s] * 0.8,
                                SchwarzschildRadius[s], Dist);
        TotalOffset = float2(0, 0);
        // Will darken below
    }
}

// Sample with displacement (4x multisample)
float3 Color = float3(0, 0, 0);
float2 Offsets[4] = { float2(0.5,0), float2(-0.5,0),
                      float2(0,0.5), float2(0,-0.5) };
for (int i = 0; i < 4; i++)
{
    float2 SampleUV = UV + TotalOffset + Offsets[i] * 0.001;
    Color += SceneTextureLookup(SampleUV, 14, false).rgb;
}
Color /= 4.0;

// Event horizon darkening
for (int s = 0; s < SMBHCount; s++)
{
    float Dist = length(UV - SMBHScreenPos[s]);
    float Shadow = smoothstep(SchwarzschildRadius[s] * 0.7,
                              SchwarzschildRadius[s] * 1.2, Dist);
    Color *= Shadow;

    // Photon ring
    float RingDist = abs(Dist - SchwarzschildRadius[s] * 1.5);
    float Ring = exp(-RingDist * RingDist / (0.0001));
    Color += float3(1.0, 0.9, 0.7) * Ring * 2.0;
}

return float4(Color, 1.0);
```

**Material Parameters (set dynamically by CosmosimSubsystem):**
- `SMBHCount` (Scalar): 0, 1, or 2
- `SMBHScreenPos` (Vector2, array of 2): screen-space UV positions
- `LensingStrength` (Scalar, array of 2): `mass * 0.002`
- `SchwarzschildRadius` (Scalar, array of 2): event horizon in screen units
```

- [ ] **Step 2: Commit**

```bash
git add ue/CosmosimPlugin/Content/README_MATERIALS.md
git commit -m "docs: add materials setup guide including PP_GravLensing HLSL"
```

---

## Task 11: Default Level Setup Guide

**Files:**
- Create: `ue/CosmosimPlugin/Content/README_LEVEL_SETUP.md`

- [ ] **Step 1: Write level setup guide**

Create `ue/CosmosimPlugin/Content/README_LEVEL_SETUP.md`:

```markdown
# Default Level Setup (CosmosimDefault)

## 1. Create Level
- File → New Level → Empty Open World (or Empty Level)
- Save as `Content/Maps/CosmosimDefault`

## 2. Post-Process Volume
- Place a Post Process Volume (infinite unbound)
- Settings:
  - **Bloom:** Intensity 0.8, Threshold 1.0
  - **Exposure:** Method = Auto Exposure Histogram,
    Min Brightness 0.5, Max Brightness 10.0
  - **Motion Blur:** Off (enable for cinematic via Sequencer override)
  - **DOF:** Off (enable for cinematic via Sequencer override)
  - **Lumen Global Illumination:** Enabled
  - **Post Process Materials:** Add `PP_GravLensing` (array element)

## 3. Skybox
- Place a BP_Sky_Sphere or use a dark cubemap texture
- Set sky to very dark / black for space background
- Optionally: add a star field cubemap for distant stars

## 4. Game Mode
- Create a Blueprint GameMode that uses:
  - Default Pawn: SpectatorPawn (or custom pawn with no mesh)
  - Player Controller: CosmosimController
- Set as the level's GameMode Override

## 5. Niagara Systems
- Place 6 Niagara System actors in the level (or spawn from CosmosimController):
  - NS_Stars, NS_Gas, NS_Jets, NS_SMBH, NS_Lobes, NS_Dust
- Each references the same `Cosmosim Particles` Data Interface user parameter

## 6. Lighting
- Delete the default directional light (space has no sun)
- Lumen GI will handle indirect lighting from emissive particles
- Optionally: very dim ambient cubemap for subtle fill

## 7. HUD
- Create a Widget Blueprint `WBP_CosmosimHUD` based on `UCosmosimHUD`
- Add 3 TextBlock widgets named: `ParticleCountText`, `SimTimeText`, `PausedText`
- Add to viewport from CosmosimController's BeginPlay (or via GameMode)

## 8. Sequencer (Cinematic Mode)
- Create a Level Sequence asset
- Add camera cut track with CineCamera actors for cinematic shots
- Use Movie Render Queue for offline rendering (Settings: Anti-Aliasing = Temporal, high sample count)
```

- [ ] **Step 2: Commit**

```bash
git add ue/CosmosimPlugin/Content/README_LEVEL_SETUP.md
git commit -m "docs: add default level setup guide for CosmosimDefault map"
```

---

## Task 12: Copy Script and Integration Smoke Test

**Files:**
- Create: `scripts/build_ue_lib.sh`

- [ ] **Step 1: Create build + copy script**

Create `scripts/build_ue_lib.sh`:

```bash
#!/bin/bash
set -euo pipefail

# Build libcosmosim shared library and copy to UE plugin ThirdParty
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
UE_THIRDPARTY="$PROJECT_ROOT/ue/CosmosimPlugin/ThirdParty/libcosmosim"

echo "=== Building libcosmosim ==="
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$PROJECT_ROOT"
cmake --build "$BUILD_DIR" --target cosmosim_shared

echo "=== Copying to UE plugin ==="
mkdir -p "$UE_THIRDPARTY/include" "$UE_THIRDPARTY/lib"

# Headers
cp "$PROJECT_ROOT/src/cosmosim_api.h" "$UE_THIRDPARTY/include/"
cp "$PROJECT_ROOT/src/body.h" "$UE_THIRDPARTY/include/"

# Library (platform-dependent)
case "$(uname)" in
    Darwin)
        cp "$BUILD_DIR/libcosmosim.dylib" "$UE_THIRDPARTY/lib/"
        echo "Copied libcosmosim.dylib"
        ;;
    Linux)
        cp "$BUILD_DIR/libcosmosim.so" "$UE_THIRDPARTY/lib/"
        echo "Copied libcosmosim.so"
        ;;
    MINGW*|CYGWIN*|MSYS*)
        cp "$BUILD_DIR/cosmosim.dll" "$UE_THIRDPARTY/lib/"
        cp "$BUILD_DIR/cosmosim.lib" "$UE_THIRDPARTY/lib/" 2>/dev/null || true
        echo "Copied cosmosim.dll"
        ;;
esac

echo "=== Running physics tests ==="
"$BUILD_DIR/test_physics"

echo ""
echo "=== Done ==="
echo "Headers: $UE_THIRDPARTY/include/"
echo "Library: $UE_THIRDPARTY/lib/"
echo ""
echo "Next: Open UE project and enable CosmosimPlugin"
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/build_ue_lib.sh
```

- [ ] **Step 3: Run the script to verify end-to-end build**

Run: `./scripts/build_ue_lib.sh`
Expected: Library builds, headers copied, all physics tests pass.

- [ ] **Step 4: Verify shared library exports**

Run (macOS): `nm -gU build/libcosmosim.dylib | grep cosmosim_`
Expected output includes:
```
T _cosmosim_create
T _cosmosim_step
T _cosmosim_get_bodies
T _cosmosim_get_count
T _cosmosim_get_active_count
T _cosmosim_get_sim_time
T _cosmosim_destroy
T _cosmosim_default_config
```

- [ ] **Step 5: Commit**

```bash
git add scripts/build_ue_lib.sh
git commit -m "feat: add build script for libcosmosim → UE plugin ThirdParty"
```

---

## Summary

| Task | Component | Type |
|------|-----------|------|
| 1 | C API header | Code |
| 2 | C API implementation + tests | Code + Test |
| 3 | Shared library CMake target | Build |
| 4 | UE plugin scaffolding | Code |
| 5 | Cosmosim Subsystem (physics thread) | Code |
| 6 | Niagara Data Interface | Code |
| 7 | Camera Controller | Code |
| 8 | HUD Widget | Code |
| 9 | Niagara systems setup | Editor guide |
| 10 | Materials setup | Editor guide |
| 11 | Level setup | Editor guide |
| 12 | Build script + smoke test | Script |

Tasks 1-3 are testable standalone (C side). Tasks 4-8 compile within UE. Tasks 9-11 are editor setup guides. Task 12 ties C build to UE plugin.
