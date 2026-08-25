// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/ProjectGameInstanceBase.h"

#include "Base/ProjectGameStateBase.h"
#include "Engine/World.h"
#include "Subsystems/WarehouseDataSubsystem.h"

void UProjectGameInstanceBase::SaveStoredItems(const TArray<FStoredItemData>& InStoredItems)
{
	SavedStoredItems = InStoredItems;
	bWarehouseInitialized = true;
}

void UProjectGameInstanceBase::SaveStoredItemInstances(const TArray<FStoredItemInstanceData>& InStoredItemInstances)
{
	SavedStoredItemInstances = InStoredItemInstances;
}

void UProjectGameInstanceBase::ClearStoredItems()
{
	SavedStoredItems.Reset();
	SavedStoredItemInstances.Reset();
}

void UProjectGameInstanceBase::SavePendingDeliveryData(const FDeliveryData& InDeliveryData)
{
	PendingDeliveryData = InDeliveryData;
}

void UProjectGameInstanceBase::ClearPendingDeliveryData()
{
	PendingDeliveryData.SelectedItems.Reset();
	PendingDeliveryData.SelectedItemInstances.Reset();
}

void UProjectGameInstanceBase::SaveEconomyData()
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();

	if (!ProjectGameState || !ProjectGameState->HasAuthority())
	{
		return;
	}

	// 게임 상태의 Gold / Reputation / Debt / DebtCycle / Economy HalfDay를
	// 레벨 이동 전에 GameInstance에 임시 보관
	SavedGold = ProjectGameState->Gold;
	SavedReputation = ProjectGameState->Reputation;
	SavedDebt = ProjectGameState->CurrentDebt;
	SavedDebtCycle = ProjectGameState->DebtCycle;
	SavedEconomyCurrentHalfDay = ProjectGameState->GetEconomyCurrentHalfDay();
	bHaveSavedEconomyData = true;
}

void UProjectGameInstanceBase::LoadEconomyData()
{
	if (!bHaveSavedEconomyData)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AProjectGameStateBase* ProjectGameState = World->GetGameState<AProjectGameStateBase>();
	if (!ProjectGameState || !ProjectGameState->HasAuthority())
	{
		return;
	}

	// 저장해 둔 경제 정보를 새 레벨의 게임 상태에 다시 적용
	ProjectGameState->SetGold(SavedGold);
	ProjectGameState->SetReputation(SavedReputation);
	ProjectGameState->SetCurrentDebt(SavedDebt);
	ProjectGameState->SetDebtCycle(SavedDebtCycle);
	ProjectGameState->SetEconomyCurrentHalfDay(SavedEconomyCurrentHalfDay);
}

void UProjectGameInstanceBase::ResetRunData()
{
	SavedGold = 0;
	SavedReputation = 0;
	SavedDebt = 0;
	SavedDebtCycle = 0;
	SavedEconomyCurrentHalfDay = 0;
	bHaveSavedEconomyData = false;
	ClearStoredItems();
	bWarehouseInitialized = false;
	ClearPendingDeliveryData();
	SavedPlayerInventories.Reset();
	if (UWarehouseDataSubsystem* WarehouseSubsystem = GetSubsystem<UWarehouseDataSubsystem>())
	{
		WarehouseSubsystem->InitializeWarehouseFromDataAsset();
	}
}

