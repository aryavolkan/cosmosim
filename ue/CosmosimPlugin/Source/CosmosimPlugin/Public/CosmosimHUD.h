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
