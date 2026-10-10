#include "UI/ThrowChargeWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

UThrowChargeWidget::UThrowChargeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UThrowChargeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Collapsed);
	UpdateCharge(0.0f);
}

void UThrowChargeWidget::SetChargingState(bool bIsCharging)
{
	if (bIsCharging)
	{
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		SetVisibility(ESlateVisibility::Collapsed);
		UpdateCharge(0.0f);
	}

	OnChargingStateChanged(bIsCharging);
}

void UThrowChargeWidget::UpdateCharge(float ChargeRatio)
{
	const float ClampedRatio = FMath::Clamp(ChargeRatio, 0.0f, 1.0f);

	if (ProgressBar_Charge)
	{
		ProgressBar_Charge->SetPercent(ClampedRatio);

		// 진행률에 따라 색상 부드럽게 보간 (최소힘 -> 최대힘)
		const FLinearColor FillColor = FMath::Lerp(MinChargeColor, MaxChargeColor, ClampedRatio);
		ProgressBar_Charge->SetFillColorAndOpacity(FillColor);
	}

	if (Text_ChargePercent)
	{
		const int32 PercentInt = FMath::RoundToInt(ClampedRatio * 100.0f);
		Text_ChargePercent->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), PercentInt)));
	}

	OnChargeUpdated(ClampedRatio);
}
