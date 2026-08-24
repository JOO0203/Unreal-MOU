// Fill out your copyright notice in the Description page of Project Settings.

#include "Base/ProjectGameInstanceBase.h"

#include "Base/ProjectGameStateBase.h"
#include "Engine/World.h"

void UProjectGameInstanceBase::SaveStoredItems(const TArray<FStoredItemData>& InStoredItems)
{
	SavedStoredItems = InStoredItems;
}

void UProjectGameInstanceBase::ClearStoredItems()
{
	SavedStoredItems.Reset();
}

void UProjectGameInstanceBase::SavePendingDeliveryData(const FDeliveryData& InDeliveryData)
{
	PendingDeliveryData = InDeliveryData;
}

void UProjectGameInstanceBase::ClearPendingDeliveryData()
{
	PendingDeliveryData.SelectedItems.Reset();
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
