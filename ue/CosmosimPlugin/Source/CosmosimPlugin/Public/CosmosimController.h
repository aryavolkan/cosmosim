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
