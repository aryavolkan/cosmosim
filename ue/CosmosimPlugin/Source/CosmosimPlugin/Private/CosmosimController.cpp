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
    OrbitAzimuth += LookDelta.X * OrbitSensitivity;
    OrbitElevation = FMath::Clamp(
        OrbitElevation + LookDelta.Y * OrbitSensitivity, -1.5f, 1.5f);
    OrbitDistance = FMath::Clamp(OrbitDistance - ZoomDelta * ZoomSpeed, 5.0f, 200.0f);

    OrbitTarget += GetPawn()->GetActorRightVector() * MoveDelta.X * 0.5f;
    OrbitTarget += GetPawn()->GetActorUpVector() * MoveDelta.Y * 0.5f;

    float CosElev = FMath::Cos(OrbitElevation);
    FVector EyePos = OrbitTarget + FVector(
        OrbitDistance * CosElev * FMath::Cos(OrbitAzimuth),
        OrbitDistance * CosElev * FMath::Sin(OrbitAzimuth),
        OrbitDistance * FMath::Sin(OrbitElevation));

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
    SmoothedTarget = SmoothedTarget * (1.0f - TrackSmoothing) + Target * TrackSmoothing;

    OrbitAzimuth += LookDelta.X * OrbitSensitivity;
    OrbitElevation = FMath::Clamp(
        OrbitElevation + LookDelta.Y * OrbitSensitivity, -1.5f, 1.5f);
    OrbitDistance = FMath::Clamp(OrbitDistance - ZoomDelta * ZoomSpeed,
                                TrackMinDistance, TrackMaxDistance);

    float Dist = FMath::Clamp(OrbitDistance, TrackMinDistance, TrackMaxDistance);
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
        if ((int)Buf[i].Type == 2)
        {
            Sum += FVector(Buf[i].PosX, Buf[i].PosY, Buf[i].PosZ);
            SMBHCount++;
        }
    }
    if (SMBHCount > 0)
        return Sum / (float)SMBHCount;

    float TotalMass = 0.0f;
    FVector CoM = FVector::ZeroVector;
    for (int i = 0; i < Count; i++)
    {
        CoM += FVector(Buf[i].PosX, Buf[i].PosY, Buf[i].PosZ) * Buf[i].Mass;
        TotalMass += Buf[i].Mass;
    }
    return TotalMass > 0.0f ? CoM / TotalMass : FVector::ZeroVector;
}
