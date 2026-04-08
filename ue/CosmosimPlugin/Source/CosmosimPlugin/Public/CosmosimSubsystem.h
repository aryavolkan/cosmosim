#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include <atomic>
#include "CosmosimSubsystem.generated.h"

extern "C" {
    #include "cosmosim_api.h"
}

struct FCosmosimBodyGPU
{
    float PosX, PosY, PosZ;
    float Mass;
    float VelX, VelY, VelZ;
    float Type;
    float Temperature;
    float Density;
    float Lifetime;
    float Luminosity;
    float SpinX, SpinY, SpinZ;
    float _Pad;
};

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

    const FCosmosimBodyGPU* GetReadBuffer() const;
    int GetActiveCount() const;
    double GetSimTime() const;

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    void PauseSim();

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    void ResumeSim();

    UFUNCTION(BlueprintCallable, Category = "Cosmosim")
    bool IsPaused() const;

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
