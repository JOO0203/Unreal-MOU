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

void UWarehouseDataSubsystem::SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances)
{
	if (UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ProjectGameInstance->SaveStoredItemInstances(InStoredItemInstances);
	}
}

TArray<FStoredItemData> UWarehouseDataSubsystem::GetStoredItemsCopy() const
{
	return GetStoredItemsInternal();
}

TArray<FStoredItemInstanceData> UWarehouseDataSubsystem::GetStoredItemInstancesCopy() const
{
	return GetStoredItemInstancesInternal();
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
	SaveStoredItemInstances(WarehouseComponent->BuildStoredItemInstanceData());
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

	if (!ConsumeStoredItemsForDelivery(DeliveryData))
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

const TArray<FStoredItemInstanceData>& UWarehouseDataSubsystem::GetStoredItemInstancesInternal() const
{
	if (const UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		return ProjectGameInstance->SavedStoredItemInstances;
	}

	static const TArray<FStoredItemInstanceData> EmptyItems;
	return EmptyItems;
}

bool UWarehouseDataSubsystem::BuildValidatedDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const
{
	OutDeliveryData.SelectedItems.Reset();
	OutDeliveryData.SelectedItemInstances.Reset();

	TSet<int32> UsedInstanceIndices;
	const TArray<FStoredItemInstanceData>& StoredItemInstances = GetStoredItemInstancesInternal();

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

		int32 AddedInstanceCount = 0;
		for (int32 InstanceIndex = 0; InstanceIndex < StoredItemInstances.Num() && AddedInstanceCount < RequestedItem.Quantity; ++InstanceIndex)
		{
			const FStoredItemInstanceData& StoredItemInstance = StoredItemInstances[InstanceIndex];
			if (UsedInstanceIndices.Contains(InstanceIndex) || StoredItemInstance.ItemClass != RequestedItem.ItemClass)
			{
				continue;
			}

			OutDeliveryData.SelectedItemInstances.Add(StoredItemInstance);
			UsedInstanceIndices.Add(InstanceIndex);
			++AddedInstanceCount;
		}

		// 이전 저장 데이터처럼 개별 상태가 없는 경우에도 배달 테스트가 막히지 않도록 클래스 기반 기본 데이터를 채웁니다.
		while (AddedInstanceCount < RequestedItem.Quantity)
		{
			FStoredItemInstanceData FallbackItemInstance;
			FallbackItemInstance.ItemClass = RequestedItem.ItemClass;
			OutDeliveryData.SelectedItemInstances.Add(FallbackItemInstance);
			++AddedInstanceCount;
		}
	}

	return !OutDeliveryData.IsEmpty();
}

bool UWarehouseDataSubsystem::ConsumeStoredItemsForDelivery(const FDeliveryData& DeliveryData)
{
	UProjectGameInstanceBase* ProjectGameInstance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	if (!ProjectGameInstance || DeliveryData.IsEmpty())
	{
		return false;
	}

	TArray<FStoredItemData> UpdatedStoredItems = ProjectGameInstance->SavedStoredItems;
	for (const FStoredItemData& SelectedItem : DeliveryData.SelectedItems)
	{
		if (!SelectedItem.ItemClass || SelectedItem.Quantity <= 0)
		{
			continue;
		}

		const int32 StoredItemIndex = UpdatedStoredItems.IndexOfByPredicate(
			[&SelectedItem](const FStoredItemData& StoredItem)
			{
				return StoredItem.ItemClass == SelectedItem.ItemClass;
			});

		if (StoredItemIndex == INDEX_NONE || UpdatedStoredItems[StoredItemIndex].Quantity < SelectedItem.Quantity)
		{
			return false;
		}

		UpdatedStoredItems[StoredItemIndex].Quantity -= SelectedItem.Quantity;
		if (UpdatedStoredItems[StoredItemIndex].Quantity <= 0)
		{
			UpdatedStoredItems.RemoveAt(StoredItemIndex);
		}
	}

	TArray<FStoredItemInstanceData> UpdatedStoredItemInstances = ProjectGameInstance->SavedStoredItemInstances;
	for (const FStoredItemInstanceData& SelectedItemInstance : DeliveryData.SelectedItemInstances)
	{
		if (!SelectedItemInstance.ItemClass)
		{
			continue;
		}

		const int32 StoredItemInstanceIndex = UpdatedStoredItemInstances.IndexOfByPredicate(
			[&SelectedItemInstance](const FStoredItemInstanceData& StoredItemInstance)
			{
				return StoredItemInstance.ItemClass == SelectedItemInstance.ItemClass;
			});

		if (StoredItemInstanceIndex != INDEX_NONE)
		{
			UpdatedStoredItemInstances.RemoveAt(StoredItemInstanceIndex);
		}
	}

	ProjectGameInstance->SaveStoredItems(UpdatedStoredItems);
	ProjectGameInstance->SaveStoredItemInstances(UpdatedStoredItemInstances);
	return true;
}
