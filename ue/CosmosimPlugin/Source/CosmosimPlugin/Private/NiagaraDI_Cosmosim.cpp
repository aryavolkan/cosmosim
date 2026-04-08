#include "NiagaraDI_Cosmosim.h"
#include "NiagaraTypes.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

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
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = GetNumBodiesName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Outputs.Add(FNiagaraVariable(
            FNiagaraTypeDefinition::GetIntDef(), TEXT("NumBodies")));
        OutFunctions.Add(Sig);
    }
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
        *OutMass.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? Buf[Idx].Mass : 0.0f;
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
        *OutType.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? (int32)Buf[Idx].Type : 0;
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
        *OutTemp.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? Buf[Idx].Temperature : 0.0f;
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
        *OutDensity.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? Buf[Idx].Density : 0.0f;
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
        *OutLifetime.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? Buf[Idx].Lifetime : 0.0f;
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
        *OutLum.GetDestAndAdvance() = (Buf && Idx >= 0 && Idx < Count) ? Buf[Idx].Luminosity : 0.0f;
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
    return true;
}
