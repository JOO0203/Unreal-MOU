#include "Item/ShopBase.h"
#include "Item/ItemSpawner.h"
#include "Base/ItemBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AShopBase::AShopBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ShopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShopMesh"));
	ShopMesh->SetupAttachment(Root);

	// 진열 슬롯들의 부모. BP에서 이 밑에 Scene Component를 자식으로 놓으면 슬롯이 된다.
	ItemAnchorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ItemAnchorRoot"));
	ItemAnchorRoot->SetupAttachment(Root);
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

// [SHOP-002] ShopItemRows를 각 슬롯에 스폰 (서버 권한에서만)
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

	// 슬롯 개수와 진열 목록 개수 중 작은 쪽만큼만 스폰
	const int32 SpawnCount = FMath::Min(SlotTransforms.Num(), ShopItemRows.Num());
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const FName RowName = ShopItemRows[Index];
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
}
