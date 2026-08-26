#include "Item/ShopBase.h"
#include "Item/ItemSpawner.h"
#include "Item/ItemSpawnRow.h"
#include "Base/ItemBase.h"
#include "Base/ProjectGameStateBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

AShopBase::AShopBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ShopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShopMesh"));
	ShopMesh->SetupAttachment(Root);

	// 리롤 버튼 메시: 눌림 연출을 위해 Movable. 위치는 BP/에디터에서 조정.
	RerollButtonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RerollButtonMesh"));
	RerollButtonMesh->SetupAttachment(Root);
	RerollButtonMesh->SetMobility(EComponentMobility::Movable);

	// 진열 슬롯들의 부모. BP에서 이 밑에 Scene Component를 자식으로 놓으면 슬롯이 된다.
	ItemAnchorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ItemAnchorRoot"));
	ItemAnchorRoot->SetupAttachment(Root);

	// 평판 구간 기본값(계단식): -40→2, -20→4, 0→5, 20→6, 40→8
	auto AddSlotRule = [this](int32 MinRep, int32 Count)
	{
		FReputationSlotRule Rule;
		Rule.MinReputation = MinRep;
		Rule.SlotCount = Count;
		SlotRules.Add(Rule);
	};
	AddSlotRule(-40, 2);
	AddSlotRule(-20, 4);
	AddSlotRule(0,   5);
	AddSlotRule(20,  6);
	AddSlotRule(40,  8);
}

void AShopBase::BeginPlay()
{
	Super::BeginPlay();

	// 스폰은 서버 권한에서만. 스폰 로직은 AItemSpawner를 재사용한다.
	if (HasAuthority())
	{
		// 표 데이터 주입 + Deferred 스폰 흐름을 공유하기 위해 내부 스포너 1개 생성
		if (!InternalSpawner)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			InternalSpawner = GetWorld()->SpawnActor<AItemSpawner>(
				AItemSpawner::StaticClass(), GetActorTransform(), Params);
		}

		if (bAutoSpawnOnBeginPlay)
		{
			SpawnShopItems();
		}
	}
}

// [SHOP-001] ItemAnchorRoot의 자식 Scene Component들을 진열 슬롯으로 수집
void AShopBase::GetSlotTransforms(TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();

	if (!ItemAnchorRoot)
	{
		return;
	}

	// 직속 자식만 슬롯으로 취급 (ItemAnchorRoot 자신은 제외)
	TArray<USceneComponent*> SlotComponents;
	ItemAnchorRoot->GetChildrenComponents(/*bIncludeAllDescendants=*/false, SlotComponents);

	for (USceneComponent* SlotComp : SlotComponents)
	{
		if (SlotComp)
		{
			OutTransforms.Add(SlotComp->GetComponentTransform());
		}
	}
}

// [SHOP-002] 평판 기반 재진열: 개수/무기수 계산 → 행 목록 구성 → 슬롯에 스폰 (서버 권한에서만)
void AShopBase::SpawnShopItems()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!InternalSpawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShopBase] InternalSpawner가 없음 (BeginPlay 전 호출?)"));
		return;
	}

	if (!ItemTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShopBase] ItemTable이 지정되지 않음"));
		return;
	}

	// 재진열 대비: 기존 진열 아이템 먼저 정리
	ClearShopItems();

	// 스폰 로직 재사용을 위해 내부 스포너에 이 상점의 표를 넘겨준다.
	InternalSpawner->SetItemTable(ItemTable);

	TArray<FTransform> SlotTransforms;
	GetSlotTransforms(SlotTransforms);

	if (SlotTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShopBase] 진열 슬롯이 없음 (ItemAnchorRoot 밑에 Scene Component를 추가하세요)"));
		return;
	}

	// 진열할 행 목록 구성
	TArray<FName> RowsToSpawn;
	if (bUseRandomSelection)
	{
		// 평판 → 진열 개수/무기 개수 산출
		const int32 Reputation = GetCurrentReputation();
		int32 DesiredCount = GetSlotCountForReputation(Reputation);
		int32 WeaponCount = GetWeaponCountForReputation(Reputation);

		// 실제 슬롯 수를 넘지 않도록 클램프
		DesiredCount = FMath::Min(DesiredCount, SlotTransforms.Num());
		WeaponCount = FMath::Min(WeaponCount, DesiredCount);

		BuildRandomRowList(DesiredCount, WeaponCount, RowsToSpawn);
	}
	else
	{
		// 수동 목록 사용
		RowsToSpawn = ShopItemRows;
	}

	// 슬롯 개수와 진열 목록 개수 중 작은 쪽만큼만 스폰
	const int32 SpawnCount = FMath::Min(SlotTransforms.Num(), RowsToSpawn.Num());
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const FName RowName = RowsToSpawn[Index];
		if (RowName.IsNone())
		{
			continue;
		}

		const FTransform& SlotTransform = SlotTransforms[Index];
		AItemBase* Spawned = InternalSpawner->SpawnItemAt(
			RowName, SlotTransform.GetLocation(), SlotTransform.Rotator());

		if (Spawned)
		{
			DisplayedItems.Add(Spawned);

			// 이 행의 가격을 DT에서 읽어 저장 (구매 시 이 값으로 SpendGold)
			int32 Price = 0;
			if (const FItemSpawnRow* Row = ItemTable->FindRow<FItemSpawnRow>(RowName, TEXT("ShopBase::SpawnShopItems Price")))
			{
				Price = Row->Price;
			}
			DisplayedItemPrices.Add(Spawned, Price);

			// 결제 전까지 집기 불가 상태로 잠금
			LockDisplayedItem(Spawned);
		}
	}
}

// [SHOP-003] 진열된 아이템 전부 제거
void AShopBase::ClearShopItems()
{
	if (!HasAuthority())
	{
		return;
	}

	for (AItemBase* Item : DisplayedItems)
	{
		if (IsValid(Item))
		{
			Item->Destroy();
		}
	}
	DisplayedItems.Reset();
	DisplayedItemPrices.Reset();
}

// [SHOP-004] 팀 공용 평판 조회 (실시간 값은 GameState에 있음)
int32 AShopBase::GetCurrentReputation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AProjectGameStateBase* GS = World->GetGameState<AProjectGameStateBase>())
		{
			return GS->Reputation;
		}
	}
	return 0;
}

// [SHOP-005] 평판 → 진열 총 개수 (계단식). 평판 이하 구간 중 가장 큰 MinReputation을 고른다.
int32 AShopBase::GetSlotCountForReputation(int32 Reputation) const
{
	int32 BestMin = TNumericLimits<int32>::Lowest();
	int32 Result = 0; // 어떤 구간에도 못 미치면 0개

	for (const FReputationSlotRule& Rule : SlotRules)
	{
		// "이 값 이상"이면서 지금까지 찾은 것보다 더 높은 하한이면 채택
		if (Reputation >= Rule.MinReputation && Rule.MinReputation >= BestMin)
		{
			BestMin = Rule.MinReputation;
			Result = Rule.SlotCount;
		}
	}
	return FMath::Max(0, Result);
}

// [SHOP-006] 평판 → 무기 개수 (음수=0, WeaponOne이상=1, WeaponTwo이상=2)
int32 AShopBase::GetWeaponCountForReputation(int32 Reputation) const
{
	if (Reputation >= WeaponTwoAtReputation)
	{
		return 2;
	}
	if (Reputation >= WeaponOneAtReputation)
	{
		return 1;
	}
	return 0;
}

// [SHOP-007] ItemTable에서 지정 카테고리 행 이름 수집
void AShopBase::CollectRowsByCategory(EItemCategory Category, TArray<FName>& OutRows) const
{
	OutRows.Reset();

	if (!ItemTable)
	{
		return;
	}

	// DataTable의 모든 행을 순회하며 카테고리가 일치하는 행만 수집
	const TMap<FName, uint8*>& RowMap = ItemTable->GetRowMap();
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		const FItemSpawnRow* Row = reinterpret_cast<const FItemSpawnRow*>(Pair.Value);
		if (Row && Row->ItemClass && Row->Category == Category)
		{
			OutRows.Add(Pair.Key);
		}
	}
}

// [SHOP-008] 후보에서 랜덤 N개 추출 (중복 허용)
void AShopBase::PickRandomRows(const TArray<FName>& Candidates, int32 Count, TArray<FName>& OutRows) const
{
	if (Candidates.Num() == 0 || Count <= 0)
	{
		return;
	}

	// 중복 허용: 매 칸 독립적으로 랜덤 인덱스 선택
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Pick = FMath::RandRange(0, Candidates.Num() - 1);
		OutRows.Add(Candidates[Pick]);
	}
}

// [SHOP-009] 평판 기반 진열 행 목록 구성: 무기 우선 배치 → 나머지 포션/기타
void AShopBase::BuildRandomRowList(int32 SlotCount, int32 WeaponCount, TArray<FName>& OutRows) const
{
	OutRows.Reset();

	if (SlotCount <= 0)
	{
		return;
	}

	// 카테고리별 후보 수집
	TArray<FName> WeaponRows;
	TArray<FName> PotionRows;
	TArray<FName> EtcRows;
	CollectRowsByCategory(EItemCategory::WEAPON, WeaponRows);
	CollectRowsByCategory(EItemCategory::POTION, PotionRows);
	CollectRowsByCategory(EItemCategory::ETC, EtcRows);

	// 포션 + 기타를 합친 "비무기" 후보 (남는 칸 채움용)
	TArray<FName> NonWeaponRows;
	NonWeaponRows.Append(PotionRows);
	NonWeaponRows.Append(EtcRows);

	// 1) 무기 우선 배치 (무기 후보가 없으면 그만큼 비무기로 채워짐)
	int32 PlacedWeapons = 0;
	if (WeaponCount > 0 && WeaponRows.Num() > 0)
	{
		const int32 WeaponToPlace = FMath::Min(WeaponCount, SlotCount);
		PickRandomRows(WeaponRows, WeaponToPlace, OutRows);
		PlacedWeapons = WeaponToPlace;
	}

	// 2) 나머지 칸은 포션/기타로 채움
	const int32 Remaining = SlotCount - PlacedWeapons;
	if (Remaining > 0)
	{
		if (NonWeaponRows.Num() > 0)
		{
			PickRandomRows(NonWeaponRows, Remaining, OutRows);
		}
		else if (WeaponRows.Num() > 0)
		{
			// 비무기 후보가 아예 없으면 폴백으로 무기라도 채운다(빈 칸 방지)
			PickRandomRows(WeaponRows, Remaining, OutRows);
		}
	}
}

// [SHOP-010] 진열 아이템 구매: 저장된 가격으로 SpendGold → 성공 시 잠금 해제 + 진열 목록에서 제거
bool AShopBase::TryPurchaseItem(AItemBase* Item, AActor* Buyer)
{
	// 결제(골드 차감)는 서버 권한에서만
	if (!HasAuthority())
	{
		return false;
	}

	if (!IsValid(Item))
	{
		return false;
	}

	// 이 상점이 진열한(=아직 안 팔린) 아이템인지 확인
	const int32* PricePtr = DisplayedItemPrices.Find(Item);
	if (!PricePtr)
	{
		// 진열 목록에 없음 = 상점 물건이 아니거나 이미 팔린 것
		return false;
	}
	const int32 Price = *PricePtr;

	// 무료(0 이하)가 아니면 팀 골드에서 차감 시도 (부족하면 false → 구매 취소)
	if (Price > 0)
	{
		const UWorld* World = GetWorld();
		AProjectGameStateBase* GS = World ? World->GetGameState<AProjectGameStateBase>() : nullptr;
		if (!GS)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ShopBase] GameState를 찾을 수 없어 결제 불가"));
			return false;
		}

		if (!GS->SpendGold(Price))
		{
			UE_LOG(LogTemp, Log, TEXT("[ShopBase] 골드 부족으로 구매 실패 (가격 %d)"), Price);
			return false;
		}
	}

	// 결제 성공 → 잠금 해제하고 진열 관리에서 제외 (이제 평소처럼 집힘)
	UnlockDisplayedItem(Item);
	DisplayedItemPrices.Remove(Item);
	DisplayedItems.Remove(Item);
	return true;
}

// [SHOP-011] 플레이어 근처(PurchaseRadius 내) 안 팔린 진열 아이템 중 가장 가까운 것
AItemBase* AShopBase::GetClosestPurchasableItem(AActor* Player) const
{
	if (!IsValid(Player))
	{
		return nullptr;
	}

	const FVector PlayerLoc = Player->GetActorLocation();
	const float RadiusSq = PurchaseRadius * PurchaseRadius;

	AItemBase* Closest = nullptr;
	float MinDistSq = RadiusSq;

	for (const TPair<TObjectPtr<AItemBase>, int32>& Pair : DisplayedItemPrices)
	{
		AItemBase* Item = Pair.Key;
		if (!IsValid(Item))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(PlayerLoc, Item->GetActorLocation());
		if (DistSq <= MinDistSq)
		{
			MinDistSq = DistSq;
			Closest = Item;
		}
	}
	return Closest;
}

// [SHOP-014] 이 상점이 진열 중이고 아직 안 팔린 아이템인지 (구매 시 목록에서 빠지므로 판매되면 false)
bool AShopBase::IsDisplayedItem(AItemBase* Item) const
{
	return IsValid(Item) && DisplayedItemPrices.Contains(Item);
}

// [SHOP-012] 진열 아이템 잠금: 물리 off + 콜리전을 집기 오버랩 채널에서 제외해 못 집게 함
void AShopBase::LockDisplayedItem(AItemBase* Item) const
{
	if (!IsValid(Item) || !Item->MeshComponent)
	{
		return;
	}

	UStaticMeshComponent* Mesh = Item->MeshComponent;

	// 슬롯에 가만히 매달리도록 물리 정지
	Mesh->SetSimulatePhysics(false);

	// 오버랩 쿼리(F 상호작용 트레이스 등)는 받되, 오브젝트 타입을 WorldStatic으로 바꿔
	// 집기 판정(CarryingComponent의 WorldDynamic/PhysicsBody/Pawn 오버랩)에 안 걸리게 한다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
}

// [SHOP-013] 진열 아이템 잠금 해제: 원래 물리/콜리전(PhysicsActor)으로 복원
void AShopBase::UnlockDisplayedItem(AItemBase* Item) const
{
	if (!IsValid(Item) || !Item->MeshComponent)
	{
		return;
	}

	UStaticMeshComponent* Mesh = Item->MeshComponent;

	// ItemBase 기본 상태(바닥에 놓인 일반 아이템)로 복원
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeRigidBody();
}
