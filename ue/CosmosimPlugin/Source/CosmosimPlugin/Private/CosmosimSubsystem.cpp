#include "CosmosimSubsystem.h"

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
