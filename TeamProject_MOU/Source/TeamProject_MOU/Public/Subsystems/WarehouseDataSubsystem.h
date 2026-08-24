#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Item/DeliveryData.h"
#include "WarehouseDataSubsystem.generated.h"

class AItemBase;
class UWarehouseComponent;

UCLASS()
class TEAMPROJECT_MOU_API UWarehouseDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 창고 요약 데이터를 GameInstance에 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void SaveStoredItems(const TArray<FStoredItemData>& InStoredItems);

	// 창고 아이템 개별 상태 데이터를 GameInstance에 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances);

	// 현재 저장된 창고 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	TArray<FStoredItemData> GetStoredItemsCopy() const;

	// 현재 저장된 창고 아이템 개별 상태 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	TArray<FStoredItemInstanceData> GetStoredItemInstancesCopy() const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	void ClearStoredItems();

	// 창고 컴포넌트의 현재 데이터를 그대로 저장
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	bool SaveFromWarehouseComponent(const UWarehouseComponent* WarehouseComponent);

	// 저장된 창고 데이터를 컴포넌트로 복원
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Storage")
	bool LoadIntoWarehouseComponent(UWarehouseComponent* WarehouseComponent) const;

	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	int32 GetStoredQuantity(TSubclassOf<AItemBase> ItemClass) const;

	UFUNCTION(BlueprintPure, Category = "Warehouse|Storage")
	bool HasEnoughStoredItems(TSubclassOf<AItemBase> ItemClass, int32 RequiredQuantity) const;

	// 요청 목록이 현재 창고 수량 기준으로 배달 가능 상태 검사
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	bool CanBuildDeliveryData(const TArray<FStoredItemData>& RequestedItems) const;

	// UI에서 만든 요청 목록을 검증한 뒤 PendingDeliveryData로 저장.
	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	bool SavePendingDeliveryDataFromRequest(const TArray<FStoredItemData>& RequestedItems);

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	void SavePendingDeliveryData(const FDeliveryData& InDeliveryData);

	// 현재 저장된 배달 선택 데이터를 복사본으로 반환
	UFUNCTION(BlueprintPure, Category = "Warehouse|Delivery")
	FDeliveryData GetPendingDeliveryDataCopy() const;

	UFUNCTION(BlueprintCallable, Category = "Warehouse|Delivery")
	void ClearPendingDeliveryData();

private:
	const TArray<FStoredItemData>& GetStoredItemsInternal() const;
	const TArray<FStoredItemInstanceData>& GetStoredItemInstancesInternal() const;
	bool BuildValidatedDeliveryData(const TArray<FStoredItemData>& RequestedItems, FDeliveryData& OutDeliveryData) const;
	bool ConsumeStoredItemsForDelivery(const FDeliveryData& DeliveryData);
};
