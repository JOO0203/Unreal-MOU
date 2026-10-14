#include "Delivery/DeliveryManager.h"

#include "Base/PackageBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "Base/ProjectGameStateBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CarryingComponent.h"
#include "Delivery/DeliveryZone.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Item/DeliveryData.h"
#include "Net/UnrealNetwork.h"
#include "Player/MainCharacter.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

ADeliveryManager::ADeliveryManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ADeliveryManager::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		InitializeFromPendingDelivery();
	}
}

bool ADeliveryManager::RegisterDeliveryPackage(APackageBase* Package)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterPackage failed: not authority. Package=%s"), *GetNameSafe(Package));
		return false;
	}
	if (!IsValid(Package))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterPackage failed: invalid package"));
		return false;
	}
	// World BeginPlay 중 액터 실행 순서는 보장되지 않으므로 스포너가 먼저 호출해도 준비되게 합니다.
	InitializeFromPendingDelivery();
	if (RegisteredZoneIDs.IsEmpty())
	{
		DiscoverPlacedZones();
	}
	// 자동 탐색과 스포너의 직접 등록이 같은 프레임에 호출될 수 있으므로 중복 등록은 성공으로 취급합니다.
	if (AssignedZones.Contains(Package))
	{
		return true;
	}
	if (PackagesWaitingForZone.Contains(Package))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] Package is registered but waiting: no zone available. Package=%s"),
			*GetNameSafe(Package));
		return true;
	}

	UClass* PackageClass = Package->GetClass();
	const int32* RequiredCount = RemainingRequiredCounts.Find(PackageClass);
	if (!RequiredCount || *RequiredCount <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Delivery] RegisterPackage failed: class is not in PendingDeliveryData. Package=%s Class=%s Total=%d"),
			*GetNameSafe(Package), *GetNameSafe(PackageClass), Progress.TotalItemCount);
		return false;
	}
	if (RegisteredCounts.FindOrAdd(PackageClass) >= *RequiredCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Delivery] RegisterPackage failed: required count already registered. Package=%s Class=%s Registered=%d Required=%d"),
			*GetNameSafe(Package), *GetNameSafe(PackageClass), RegisteredCounts.FindRef(PackageClass), *RequiredCount);
		return false;
	}

	++RegisteredCounts.FindOrAdd(PackageClass);
	if (Package->MeshComponent)
	{
		// 기존 운반 시스템이 PhysicsBody 오브젝트 타입을 검색하므로 PhysicsActor 프로필을 유지합니다.
		// 배달 대상 여부는 충돌 채널이 아니라 AssignedZones 등록 정보로 판별합니다.
		Package->MeshComponent->SetGenerateOverlapEvents(true);
	}
	if (!AssignRandomZone(Package))
	{
		PackagesWaitingForZone.Add(Package);
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] Package registered, waiting for zone. Package=%s RegisteredZones=%d"),
			*GetNameSafe(Package), RegisteredZoneIDs.Num());
	}
	return true;
}

bool ADeliveryManager::RegisterDeliveryZone(FName ZoneID)
{
	if (!HasAuthority() || ZoneID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Delivery] RegisterZone failed. Authority=%d Zone=%s"),
			HasAuthority(), *ZoneID.ToString());
		return false;
	}
	if (RegisteredZoneIDs.Contains(ZoneID)) return true;

	RegisteredZoneIDs.Add(ZoneID);
	AssignWaitingPackages();
	return true;
}

void ADeliveryManager::DiscoverPlacedZones()
{
	if (!HasAuthority()) return;

	for (TActorIterator<ADeliveryZone> It(GetWorld()); It; ++It)
	{
		RegisterDeliveryZone(It->GetEffectiveZoneID());
	}

}

FName ADeliveryManager::GetAssignedZoneID(const APackageBase* Package) const
{
	if (!Package) return NAME_None;
	if (const FName* ZoneID = AssignedZones.Find(Package))
	{
		return *ZoneID;
	}
	return NAME_None;
}

bool ADeliveryManager::IsPackageAssignedToZone(const APackageBase* Package, FName ZoneID) const
{
	return !ZoneID.IsNone() && GetAssignedZoneID(Package) == ZoneID;
}

bool ADeliveryManager::IsPackageWaitingForZone(const APackageBase* Package) const
{
	return Package && PackagesWaitingForZone.Contains(Package);
}

bool ADeliveryManager::AssignRandomZone(APackageBase* Package)
{
	if (!IsValid(Package) || RegisteredZoneIDs.IsEmpty()) return false;

	const FName ZoneID = RegisteredZoneIDs[FMath::RandRange(0, RegisteredZoneIDs.Num() - 1)];
	AssignedZones.Add(Package, ZoneID);
	return true;
}

void ADeliveryManager::AssignWaitingPackages()
{
	if (RegisteredZoneIDs.IsEmpty()) return;

	for (int32 Index = PackagesWaitingForZone.Num() - 1; Index >= 0; --Index)
	{
		APackageBase* Package = PackagesWaitingForZone[Index].Get();
		if (!IsValid(Package) || AssignRandomZone(Package))
		{
			PackagesWaitingForZone.RemoveAtSwap(Index);
		}
	}
}

// [DELIVERY-000] 배달 맵에서 선택 목록을 읽고 스포너가 사용한 다음 영속 목록을 소비합니다.
void ADeliveryManager::InitializeFromPendingDelivery()
{
	if (bPendingInitialized) return;
	bPendingInitialized = true;

	UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>();
	if (!GameInstance)
	{
		bPendingInitialized = false;
		return;
	}

	// 초기화 뒤 원본 선택 목록이 비워져도 배달 진행 데이터가 유지되도록 복사합니다.
	const FDeliveryData Data = GameInstance->PendingDeliveryData;
	if (!Data.SelectedItemInstances.IsEmpty())
	{
		for (const FStoredItemInstanceData& Item : Data.SelectedItemInstances)
		{
			UClass* PackageClass = Item.ItemClass.Get();
			if (PackageClass && PackageClass->IsChildOf(APackageBase::StaticClass()))
			{
				++RemainingRequiredCounts.FindOrAdd(PackageClass);
				++Progress.TotalItemCount;
			}
		}
	}
	else
	{
		for (const FStoredItemData& Item : Data.SelectedItems)
		{
			UClass* PackageClass = Item.ItemClass.Get();
			if (PackageClass && PackageClass->IsChildOf(APackageBase::StaticClass()) && Item.Quantity > 0)
			{
				RemainingRequiredCounts.FindOrAdd(PackageClass) += Item.Quantity;
				Progress.TotalItemCount += Item.Quantity;
			}
		}
	}

	BroadcastProgress();

	if (!Data.IsEmpty())
	{
		// BP_DeliverItemSpawner도 BeginPlay에서 같은 목록을 읽습니다.
		// 모든 액터의 BeginPlay가 끝난 다음 틱에 원본을 소비하여 스폰 순서와 무관하게 만듭니다.
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UProjectGameInstanceBase* Instance = GetGameInstance<UProjectGameInstanceBase>())
				{
					Instance->ClearPendingDeliveryData();
				}
			}));
	}
}

bool ADeliveryManager::TryDeliverPackage(APackageBase* Package)
{
	if (!HasAuthority() || !IsValid(Package) || ProcessedPackages.Contains(Package)) return false;

	int32* RemainingCount = RemainingRequiredCounts.Find(Package->GetClass());
	if (!RemainingCount || *RemainingCount <= 0) return false;

	ProcessedPackages.Add(Package);
	AssignedZones.Remove(Package);
	--(*RemainingCount);
	const bool bDeliveredIntact = !Package->bIsBroken;
	if (bDeliveredIntact)
	{
		++Progress.DeliveredItemCount;
		const int32 DeliveryValue = FMath::Max(0, Package->GetCurrentValue());
		Progress.EarnedGold += DeliveryValue;
		const int32 ExistingIndex = Progress.DeliveredItems.IndexOfByPredicate(
			[Package](const FSettlementItemEntry& Entry)
			{
				return Entry.ItemClass == Package->GetClass();
			});
		if (ExistingIndex != INDEX_NONE)
		{
			++Progress.DeliveredItems[ExistingIndex].Quantity;
			Progress.DeliveredItems[ExistingIndex].EarnedGold += DeliveryValue;
		}
		else
		{
			FSettlementItemEntry Entry;
			Entry.ItemClass = Package->GetClass();
			Entry.ItemName = Package->ItemName;
			Entry.ItemIcon = Package->ItemIcon;
			Entry.Quantity = 1;
			Entry.EarnedGold = DeliveryValue;
			Progress.DeliveredItems.Add(MoveTemp(Entry));
		}
		RecordPlayerDelivery(Package, DeliveryValue);
		if (AProjectGameStateBase* GameState = GetWorld()->GetGameState<AProjectGameStateBase>())
		{
			GameState->AddGold(DeliveryValue);
		}
	}
	else
	{
		++Progress.BrokenItemCount;
	}

	ClearPackageFromPlayerHands(Package);
	Package->Destroy();
	BroadcastProgress();
	return bDeliveredIntact;
}

void ADeliveryManager::RecordPlayerDelivery(APackageBase* Package, int32 DeliveryValue)
{
	if (!IsValid(Package)) return;

	// 약탈 정산과 같은 규칙: 아이템을 마지막으로 소유(집어서 운반)한 한 명에게 귀속한다.
	// 배달 구역에 내려놓고 떠난 뒤 타이머가 완료되어도 LastOwner는 유지된다.
	const AMainCharacter* OwnerCharacter = Cast<AMainCharacter>(Package->LastOwner);
	if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsPlayerControlled()) return;

	const APlayerState* PlayerState = OwnerCharacter->GetPlayerState();
	const int32 PlayerId = PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE;
	const FString PlayerName = PlayerState ? PlayerState->GetPlayerName() : OwnerCharacter->GetName();
	int32 PlayerIndex = Progress.PlayerResults.IndexOfByPredicate(
		[PlayerId, &PlayerName](const FPlayerSettlementData& Player)
	{
		return Player.PlayerId == PlayerId && Player.PlayerName == PlayerName;
	});
	if (PlayerIndex == INDEX_NONE)
	{
		FPlayerSettlementData Player;
		Player.PlayerId = PlayerId;
		Player.PlayerName = PlayerName;
		PlayerIndex = Progress.PlayerResults.Add(MoveTemp(Player));
	}

	FPlayerSettlementData& Player = Progress.PlayerResults[PlayerIndex];
	++Player.DeliveredItemCount;
	Player.EarnedGold += DeliveryValue;
	const int32 ItemIndex = Player.DeliveredItems.IndexOfByPredicate(
		[Package](const FSettlementItemEntry& Entry)
		{
			return Entry.ItemClass == Package->GetClass();
		});
	if (ItemIndex != INDEX_NONE)
	{
		++Player.DeliveredItems[ItemIndex].Quantity;
		Player.DeliveredItems[ItemIndex].EarnedGold += DeliveryValue;
	}
	else
	{
		FSettlementItemEntry Entry;
		Entry.ItemClass = Package->GetClass();
		Entry.ItemName = Package->ItemName;
		Entry.ItemIcon = Package->ItemIcon;
		Entry.Quantity = 1;
		Entry.EarnedGold = DeliveryValue;
		Player.DeliveredItems.Add(MoveTemp(Entry));
	}
}

void ADeliveryManager::ClearPackageFromPlayerHands(APackageBase* Package)
{
	if (!HasAuthority() || !IsValid(Package)) return;

	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		AMainCharacter* Character = *It;
		UCarryingComponent* CarryingComponent = Character->GetCarryingComponent();
		if (!CarryingComponent || CarryingComponent->GetCarriedActor() != Package) continue;

		CarryingComponent->ClearCarriedItem();
		Character->ChangeEmotion(0);
	}
}

void ADeliveryManager::BroadcastProgress()
{
	++Progress.Revision;
	OnDeliveryProgressChanged.Broadcast(Progress);
	if (Progress.IsComplete() && !bCompletionBroadcast)
	{
		bCompletionBroadcast = true;
		OnAllDeliveryItemsProcessed.Broadcast(Progress);
	}
	ForceNetUpdate();
}

ADeliveryManager* ADeliveryManager::GetDeliveryManager(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ADeliveryManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ADeliveryManager::OnRep_Progress()
{
	OnDeliveryProgressChanged.Broadcast(Progress);
	if (Progress.IsComplete())
	{
		OnAllDeliveryItemsProcessed.Broadcast(Progress);
	}
}

void ADeliveryManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeliveryManager, Progress);
}
