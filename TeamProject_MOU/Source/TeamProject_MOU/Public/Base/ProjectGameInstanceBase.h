// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Item/DeliveryData.h"
#include "ProjectGameInstanceBase.generated.h"

UCLASS()
class TEAMPROJECT_MOU_API UProjectGameInstanceBase : public UGameInstance
{
	GENERATED_BODY()

public:
	// 레벨 이동 후에도 유지할 창고  데이터
	UPROPERTY(BlueprintReadOnly, Category = "Storage|Save")
	TArray<FStoredItemData> SavedStoredItems;

	// 배달 맵으로 가져갈 아이템
	UPROPERTY(BlueprintReadOnly, Category = "Delivery|Save")
	FDeliveryData PendingDeliveryData;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedGold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedReputation = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	int32 SavedDebtCycle = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Economy|Save")
	bool bHaveSavedEconomyData = false;

	UFUNCTION(BlueprintCallable, Category = "Storage|Save")
	void SaveStoredItems(const TArray<FStoredItemData>& InStoredItems);

	UFUNCTION(BlueprintCallable, Category = "Storage|Save")
	void ClearStoredItems();

	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void SaveEconomyData();

	UFUNCTION(BlueprintCallable, Category = "Economy|Save")
	void LoadEconomyData();

	UFUNCTION(BlueprintCallable, Category = "Delivery|Save")
	void SavePendingDeliveryData(const FDeliveryData& InDeliveryData);

	UFUNCTION(BlueprintCallable, Category = "Delivery|Save")
	void ClearPendingDeliveryData();
};
