#include "Components/WarehouseComponent.h"

#include "Base/ItemBase.h"
#include "Subsystems/WarehouseDataSubsystem.h"

UWarehouseComponent::UWarehouseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWarehouseComponent::CanStoreItemClass(TSubclassOf<AItemBase> ItemClass) const
{
	return ItemClass && ItemClass->IsChildOf(AItemBase::StaticClass());
}

bool UWarehouseComponent::CanStoreItemInstance(const AItemBase* ItemInstance) const
{
	return ItemInstance && CanStoreItemClass(ItemInstance->GetClass());
}

bool UWarehouseComponent::AddStoredItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity)
{
	if (!CanStoreItemClass(ItemClass) || Quantity <= 0)
	{
		return false;
	}

	const int32 ExistingIndex = FindStoredItemIndex(ItemClass);
	if (ExistingIndex != INDEX_NONE)
	{
		StoredItems[ExistingIndex].Quantity += Quantity;
	}
	else
	{
		FStoredItemData NewItem;
		NewItem.ItemClass = ItemClass;
		NewItem.Quantity = Quantity;
		StoredItems.Add(NewItem);
	}

	BroadcastWarehouseChanged();
	return true;
}

bool UWarehouseComponent::AddStoredItemFromInstance(const AItemBase* ItemInstance, int32 Quantity)
{
	return ItemInstance && AddStoredItem(ItemInstance->GetClass(), Quantity);
}

bool UWarehouseComponent::RemoveStoredItem(TSubclassOf<AItemBase> ItemClass, int32 Quantity)
{
	if (!ItemClass || Quantity <= 0)
	{
		return false;
	}

	const int32 ExistingIndex = FindStoredItemIndex(ItemClass);
	if (ExistingIndex == INDEX_NONE || StoredItems[ExistingIndex].Quantity < Quantity)
	{
		return false;
	}

	StoredItems[ExistingIndex].Quantity -= Quantity;
	if (StoredItems[ExistingIndex].Quantity <= 0)
	{
		StoredItems.RemoveAt(ExistingIndex);
	}

	BroadcastWarehouseChanged();
	return true;
}

int32 UWarehouseComponent::GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const
{
	const int32 ExistingIndex = FindStoredItemIndex(ItemClass);
	return ExistingIndex != INDEX_NONE ? StoredItems[ExistingIndex].Quantity : 0;
}

bool UWarehouseComponent::ContainsItem(TSubclassOf<AItemBase> ItemClass) const
{
	return GetStoredQuantity(ItemClass) > 0;
}

void UWarehouseComponent::ClearStoredItems()
{
	StoredItems.Reset();
	StoredItemInstances.Reset();
	BroadcastWarehouseChanged();
}

bool UWarehouseComponent::HandleActorEnteredWarehouse(AActor* OtherActor)
{
	AItemBase* ItemInstance = Cast<AItemBase>(OtherActor);
	if (!CanStoreItemInstance(ItemInstance) || StoredItemInstances.Contains(ItemInstance))
	{
		return false;
	}

	StoredItemInstances.Add(ItemInstance);
	return AddStoredItemFromInstance(ItemInstance, 1);
}

bool UWarehouseComponent::HandleActorExitedWarehouse(AActor* OtherActor)
{
	AItemBase* ItemInstance = Cast<AItemBase>(OtherActor);
	if (!ItemInstance)
	{
		return false;
	}

	const int32 RemovedCount = StoredItemInstances.Remove(ItemInstance);
	if (RemovedCount <= 0)
	{
		return false;
	}

	return RemoveStoredItem(ItemInstance->GetClass(), RemovedCount);
}

bool UWarehouseComponent::SaveStoredItemsToGameInstance()
{
	// 실제 저장 책임은 GameInstanceSubsystem이 맡고, 컴포넌트는 현재 데이터만 넘겨줍니다.
	if (UWarehouseDataSubsystem* WarehouseSubsystem = GetWorld() ? GetWorld()->GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr)
	{
		return WarehouseSubsystem->SaveFromWarehouseComponent(this);
	}

	return false;
}

bool UWarehouseComponent::LoadStoredItemsFromGameInstance()
{
	// 로비처럼 오브젝트를 다시 스폰하지 않는 맵에서는 요약 데이터만 복원
	if (UWarehouseDataSubsystem* WarehouseSubsystem = GetWorld() ? GetWorld()->GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr)
	{
		return WarehouseSubsystem->LoadIntoWarehouseComponent(this);
	}

	return false;
}

bool UWarehouseComponent::BuildDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const
{
	OutDeliveryData.SelectedItems.Reset();

	for (const FStoredItemData& RequestedItem : RequestedItems)
	{
		if (!RequestedItem.ItemClass || RequestedItem.Quantity <= 0)
		{
			continue;
		}

	
		if (GetStoredQuantity(RequestedItem.ItemClass) < RequestedItem.Quantity)
		{
			OutDeliveryData.SelectedItems.Reset();
			return false;
		}

		OutDeliveryData.SelectedItems.Add(RequestedItem);
	}

	return !OutDeliveryData.IsEmpty();
}

int32 UWarehouseComponent::FindStoredItemIndex(TSubclassOf<AItemBase> ItemClass) const
{
	for (int32 Index = 0; Index < StoredItems.Num(); ++Index)
	{
		if (StoredItems[Index].ItemClass == ItemClass)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void UWarehouseComponent::BroadcastWarehouseChanged()
{
	OnWarehouseItemsChanged.Broadcast();
}
