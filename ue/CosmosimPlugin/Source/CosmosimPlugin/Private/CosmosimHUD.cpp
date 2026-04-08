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
