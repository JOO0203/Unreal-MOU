#include "Subsystems/WarehouseDataSubsystem.h"

#include "Base/ProjectGameInstanceBase.h"
#include "Components/WarehouseComponent.h"

void UWarehouseDataSubsystem::SaveStoredItems(const TArray<FStoredItemData>& InStoredItems)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SaveStoredItems(InStoredItems);
	}
}

TArray<FStoredItemData> UWarehouseDataSubsystem::GetStoredItemsCopy() const
{
	return GetStoredItemsInternal();
}

void UWarehouseDataSubsystem::ClearStoredItems()
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->ClearStoredItems();
	}
}

bool UWarehouseDataSubsystem::SaveFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent)
{
	if (!WarehouseComponent)
	{
		return false;
	}

	SaveStoredItems(WarehouseComponent->StoredItems);
	return true;
}

bool UWarehouseDataSubsystem::LoadIntoWarehouseComponent(UWarehouseComponent* WarehouseComponent) const
{
	if (!WarehouseComponent)
	{
		return false;
	}

	// 저장된 요약 데이터만 복원하고, 실제 액터 인스턴스 목록은 새 맵 기준으로 비워 둡니다.
	WarehouseComponent->StoredItems = GetStoredItemsInternal();
	WarehouseComponent->StoredItemInstances.Reset();
	WarehouseComponent->OnWarehouseItemsChanged.Broadcast();
	return true;
}

int32 UWarehouseDataSubsystem::GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const
{
	if (!ItemClass)
	{
		return 0;
	}

	for (const FStoredItemData& StoredItem : GetStoredItemsInternal())
	{
		if (StoredItem.ItemClass == ItemClass)
		{
			return StoredItem.Quantity;
		}
	}

	return 0;
}

bool UWarehouseDataSubsystem::HasEnoughStoredItems(TSubclassOf<AItemBase> ItemClass, int32 RequiredQuantity) const
{
	return RequiredQuantity > 0 && GetStoredQuantity(ItemClass) >= RequiredQuantity;
}

bool UWarehouseDataSubsystem::CanBuildDeliveryData(const TArray<FStoredItemData>& RequestedItems) const
{
	FDeliveryData DummyData;
	return BuildValidatedDeliveryData(RequestedItems, DummyData);
}

bool UWarehouseDataSubsystem::SavePendingDeliveryDataFromRequest(const TArray<FStoredItemData>& RequestedItems)
{
	FDeliveryData DeliveryData;
	if (!BuildValidatedDeliveryData(RequestedItems, DeliveryData))
	{
		return false;
	}

	SavePendingDeliveryData(DeliveryData);
	return true;
}

void UWarehouseDataSubsystem::SavePendingDeliveryData(const FDeliveryData& InDeliveryData)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SavePendingDeliveryData(InDeliveryData);
	}
}

FDeliveryData UWarehouseDataSubsystem::GetPendingDeliveryDataCopy() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->PendingDeliveryData;
	}

	return FDeliveryData();
}

void UWarehouseDataSubsystem::ClearPendingDeliveryData()
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->ClearPendingDeliveryData();
	}
}

const TArray<FStoredItemData>& UWarehouseDataSubsystem::GetStoredItemsInternal() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->SavedStoredItems;
	}

	static const TArray<FStoredItemData> EmptyItems;
	return EmptyItems;
}

bool UWarehouseDataSubsystem::BuildValidatedDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const
{
	OutDeliveryData.SelectedItems.Reset();

	for (const FStoredItemData& RequestedItem : RequestedItems)
	{
		if (!RequestedItem.ItemClass || RequestedItem.Quantity <= 0)
		{
			continue;
		}

		// 창고에 없는 수량을 요청하면 전체 요청을 실패 처리합니다.
		if (GetStoredQuantity(RequestedItem.ItemClass) < RequestedItem.Quantity)
		{
			OutDeliveryData.SelectedItems.Reset();
			return false;
		}

		OutDeliveryData.SelectedItems.Add(RequestedItem);
	}

	return !OutDeliveryData.IsEmpty();
}
