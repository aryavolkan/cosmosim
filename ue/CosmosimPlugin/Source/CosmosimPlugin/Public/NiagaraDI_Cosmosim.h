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
