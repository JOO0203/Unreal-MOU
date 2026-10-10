#include "UI/ItemInfoWidget.h"
#include "Components/TextBlock.h"
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"

void UItemInfoWidget::NativeDestruct()
{
	if (TrackedItem.IsValid())
	{
		TrackedItem->OnDurabilityChanged.RemoveDynamic(this, &UItemInfoWidget::HandleTrackedItemDurabilityChanged);
	}
	TrackedItem.Reset();

	Super::NativeDestruct();
}

void UItemInfoWidget::UpdateItemInfo(AItemBase* Item)
{
	if (TrackedItem.Get() != Item)
	{
		if (TrackedItem.IsValid())
		{
			TrackedItem->OnDurabilityChanged.RemoveDynamic(this, &UItemInfoWidget::HandleTrackedItemDurabilityChanged);
		}

		TrackedItem = Item;

		if (TrackedItem.IsValid())
		{
			TrackedItem->OnDurabilityChanged.AddDynamic(this, &UItemInfoWidget::HandleTrackedItemDurabilityChanged);
		}
	}

	if (!Item)
	{
		return;
	}

	// 1. 이름 설정
	if (Text_Name)
	{
		Text_Name->SetText(Item->ItemName);
	}

	// 2. 내구도 및 가치 설정
	HandleTrackedItemDurabilityChanged(Item->CurrentDurability, Item->MaxDurability);
}

void UItemInfoWidget::HandleTrackedItemDurabilityChanged(float NewDurability, float NewMaxDurability)
{
	AItemBase* Item = TrackedItem.Get();
	if (!Item)
	{
		return;
	}

	// 내구도 설정
	if (Text_Durability)
	{
		FString DurabilityStr = FString::Printf(TEXT("Durability: %d / %d"), FMath::RoundToInt(NewDurability), FMath::RoundToInt(NewMaxDurability));
		Text_Durability->SetText(FText::FromString(DurabilityStr));

		// 내구도 경고 색상 변경 (기본값: 흰색, 경고: 빨간색)
		if (NewDurability <= DurabilityWarningThreshold)
		{
			Text_Durability->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else
		{
			Text_Durability->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	}

	// 가치 설정 (택배일 경우에만 표시)
	if (Text_Value)
	{
		APackageBase* Package = Cast<APackageBase>(Item);
		if (Package)
		{
			FString ValueStr = FString::Printf(TEXT("Value: %d"), Package->GetCurrentValue());
			Text_Value->SetText(FText::FromString(ValueStr));
			Text_Value->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// 택배가 아니면 가치 텍스트를 숨김
			Text_Value->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 블루프린트 이벤트 호출
	OnItemInfoUpdated(Item);
}
