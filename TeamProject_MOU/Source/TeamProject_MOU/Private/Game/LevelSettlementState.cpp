#include "Game/LevelSettlementState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ALevelSettlementState::ALevelSettlementState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

bool ALevelSettlementState::FinalizeSettlement(const FLevelSettlementData& Result)
{
	if (!HasAuthority() || Snapshot.bFinalized) return false;

	Snapshot.Result = Result;
	Snapshot.Result.EarnedGold = FMath::Max(0, Snapshot.Result.EarnedGold);
	Snapshot.Result.DeliveredItemCount = FMath::Max(0, Snapshot.Result.DeliveredItemCount);
	Snapshot.Result.FailedDeliveryCount = FMath::Max(0, Snapshot.Result.FailedDeliveryCount);
	Snapshot.Result.DeathCount = FMath::Max(0, Snapshot.Result.DeathCount);
	Snapshot.Result.LevelPlayTimeSeconds = FMath::Max(0.0f, Snapshot.Result.LevelPlayTimeSeconds);
	Snapshot.Result.FinalThreatLevel = FMath::Clamp(Snapshot.Result.FinalThreatLevel, 0.0f, 1.0f);
	Snapshot.bFinalized = true;
	++Snapshot.Revision;

	OnSettlementFinalized.Broadcast(Snapshot.Result);
	ForceNetUpdate();
	return true;
}

ALevelSettlementState* ALevelSettlementState::GetLevelSettlementState(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ALevelSettlementState> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ALevelSettlementState::OnRep_Snapshot()
{
	if (Snapshot.bFinalized)
	{
		OnSettlementFinalized.Broadcast(Snapshot.Result);
	}
}

void ALevelSettlementState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALevelSettlementState, Snapshot);
}
