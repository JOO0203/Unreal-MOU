#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Item/ItemSpawnRow.h" // EItemCategory
#include "ShopBase.generated.h"

class AItemBase;
class AItemSpawner;
class UDataTable;
class USceneComponent;
class UStaticMeshComponent;

/**
 * FReputationSlotRule
 * 평판 구간 → 진열 개수 매핑 한 줄. 계단식으로 동작한다.
 * "MinReputation 이상"이면 이 규칙 적용. 규칙들을 MinReputation 오름차순으로 정렬해두고
 * 현재 평판 이하 중 가장 큰 구간을 고른다.
 */
USTRUCT(BlueprintType)
struct FReputationSlotRule
{
	GENERATED_BODY()

	// 이 구간의 하한 평판 (이 값 이상이면 적용). 예: -40, -20, 0, 20, 40
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 MinReputation = 0;

	// 이 구간에서 진열할 총 아이템 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 SlotCount = 5;
};

/**
 * AShopBase
 * 상점(배달 트럭)의 C++ 베이스. BP_Shop의 부모 클래스로 사용한다.
 *
 * 역할(C++ 담당):
 *   1) 진열 지점 관리 - BP에서 ItemAnchorRoot 밑에 Scene Component를 자식으로 놓으면
 *      그게 진열 슬롯이 된다. 트럭 짐칸 형상에 맞춰 위치를 눈으로 조정 가능.
 *   2) 평판 기반 재진열 - 팀 공용 평판(AProjectGameStateBase::Reputation)을 읽어
 *      진열 개수와 무기 개수를 정하고, DT_Item에서 카테고리별로 랜덤 추출해 스폰한다.
 *
 * 골드 차감(구매/리롤 비용)과 리롤 횟수 제한은 BP에서 처리한다. 여기서는 재진열만 제공한다.
 * 스폰은 서버 권한에서만 한다(멀티 대응, 스폰 액터는 클라로 복제됨).
 */
UCLASS()
class TEAMPROJECT_MOU_API AShopBase : public AActor
{
	GENERATED_BODY()

public:
	AShopBase();

protected:
	virtual void BeginPlay() override;

#pragma region [SHOP] 컴포넌트
	// 루트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	// 트럭 메시 (BP에서 지정)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShopMesh;

	// 리롤 버튼 메시 (Movable). 위치는 BP/에디터에서 조정. 눌림 연출·클릭 판정은 BP에서.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RerollButtonMesh;

	// 진열 슬롯들의 부모. BP에서 이 밑에 Scene Component를 자식으로 놓으면 그게 진열 지점이 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> ItemAnchorRoot;
#pragma endregion

#pragma region [SHOP] 설정값
	// 아이템 데이터 테이블 (행 구조 = FItemSpawnRow). 상점에서 스폰할 아이템의 원본 데이터.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TObjectPtr<UDataTable> ItemTable;

	// 랜덤 진열 사용 여부. true(기본)면 DT에서 카테고리 규칙에 맞춰 랜덤 추출.
	// false면 아래 ShopItemRows 수동 목록을 순서대로 진열한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bUseRandomSelection = true;

	// 수동 진열 목록 (bUseRandomSelection=false일 때만 사용). ItemTable의 행 이름을 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (EditCondition = "!bUseRandomSelection"))
	TArray<FName> ShopItemRows;

	// 평판 구간 → 진열 개수 규칙(계단식). MinReputation 오름차순으로 넣는 것을 권장.
	// 기본값: -40→2, -20→4, 0→5, 20→6, 40→8 (생성자에서 세팅).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Reputation")
	TArray<FReputationSlotRule> SlotRules;

	// 무기 1개가 보장되는 최소 평판 (이 값 이상이면 무기 최소 1개). 기본 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Reputation")
	int32 WeaponOneAtReputation = 0;

	// 무기 2개가 보장되는 최소 평판 (이 값 이상이면 무기 2개). 기본 40.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Reputation")
	int32 WeaponTwoAtReputation = 40;

	// BeginPlay에 진열 아이템을 자동 스폰할지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bAutoSpawnOnBeginPlay = true;

	// 구매 반경. 플레이어가 이 거리 안에 있어야 F로 진열 아이템을 살 수 있다(GetClosestPurchasableItem 기준).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	float PurchaseRadius = 200.0f;
#pragma endregion

#pragma region [SHOP] 내부 상태
	// 스폰 로직 재사용을 위한 스포너 인스턴스(서버에서 BeginPlay에 1개 생성).
	UPROPERTY()
	TObjectPtr<AItemSpawner> InternalSpawner;

	// 이번에 진열된 아이템들(스폰 결과). 재진열/정리 시 사용.
	UPROPERTY()
	TArray<TObjectPtr<AItemBase>> DisplayedItems;

	// 진열 아이템 → 판매 가격(DT의 Price). 스폰 시 저장, 구매 시 이 값으로 SpendGold 한다.
	// (아이템에서 DT로 역참조가 안 되므로 상점이 스폰 시점의 가격을 기억해둔다)
	UPROPERTY()
	TMap<TObjectPtr<AItemBase>, int32> DisplayedItemPrices;
#pragma endregion

public:
#pragma region [SHOP] 진열 함수
	// [SHOP-001] 진열 슬롯 트랜스폼 목록을 반환 (ItemAnchorRoot의 자식 Scene Component들)
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void GetSlotTransforms(TArray<FTransform>& OutTransforms) const;

	// [SHOP-002] 현재 팀 평판을 읽어 진열 아이템을 다시 뽑아 스폰 (서버 권한에서만).
	// 리롤 버튼에서 골드 차감 성공 후 이 함수를 호출하면 된다. 기존 진열은 먼저 정리한다.
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SpawnShopItems();

	// [SHOP-003] 진열된 아이템 전부 제거 (재진열 전 정리용)
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void ClearShopItems();
#pragma endregion

#pragma region [SHOP] 구매
	// [SHOP-010] 진열 아이템 구매 (서버 권한에서만). 저장된 가격으로 SpendGold 시도 →
	//   성공하면 잠금 해제(물리/콜리전 원복)하고 진열 목록에서 빼고 true. 골드 부족/대상 아님이면 false.
	//   BP의 F 상호작용에서 GetClosestPurchasableItem으로 대상을 받아 이 함수를 호출한다.
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool TryPurchaseItem(AItemBase* Item, AActor* Buyer);

	// [SHOP-011] 플레이어에서 PurchaseRadius 안에 있는, 아직 안 팔린 진열 아이템 중 가장 가까운 것 반환.
	//   없으면 nullptr. (조준 방식으로 전환 후 미사용이지만 남겨둠)
	UFUNCTION(BlueprintPure, Category = "Shop")
	AItemBase* GetClosestPurchasableItem(AActor* Player) const;

	// [SHOP-014] 이 상점이 진열 중이고 아직 안 팔린 아이템인지. BP가 조준 대상이 이 상점 진열품인지 판정용.
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool IsDisplayedItem(AItemBase* Item) const;
#pragma endregion

#pragma region [SHOP] 평판 조회
	// [SHOP-004] 현재 팀 공용 평판을 반환 (AProjectGameStateBase::Reputation). GameState 없으면 0.
	UFUNCTION(BlueprintPure, Category = "Shop")
	int32 GetCurrentReputation() const;

	// [SHOP-005] 평판 → 진열 총 개수 (SlotRules 계단식 적용). 평판이 모든 구간보다 낮으면 0.
	UFUNCTION(BlueprintPure, Category = "Shop")
	int32 GetSlotCountForReputation(int32 Reputation) const;

	// [SHOP-006] 평판 → 무기 개수 (음수=0, WeaponOne이상=1, WeaponTwo이상=2)
	UFUNCTION(BlueprintPure, Category = "Shop")
	int32 GetWeaponCountForReputation(int32 Reputation) const;
#pragma endregion

private:
#pragma region [SHOP] 내부 헬퍼
	// [SHOP-007] ItemTable에서 지정 카테고리에 해당하는 행 이름들을 수집
	void CollectRowsByCategory(EItemCategory Category, TArray<FName>& OutRows) const;

	// [SHOP-008] 후보 목록에서 랜덤 N개를 뽑아 OutRows에 추가 (중복 허용). 후보가 비면 아무것도 안 함.
	void PickRandomRows(const TArray<FName>& Candidates, int32 Count, TArray<FName>& OutRows) const;

	// [SHOP-009] 평판 기반으로 이번에 진열할 행 이름 목록을 구성 (무기 우선 배치 + 나머지 포션/기타)
	void BuildRandomRowList(int32 SlotCount, int32 WeaponCount, TArray<FName>& OutRows) const;

	// [SHOP-012] 진열 아이템 잠금: 물리 off + 콜리전을 집기 오버랩 채널에서 제외(WorldStatic)해 못 집게 함
	void LockDisplayedItem(AItemBase* Item) const;

	// [SHOP-013] 진열 아이템 잠금 해제: 원래 물리/콜리전(PhysicsActor)으로 복원해 평소처럼 집히게 함
	void UnlockDisplayedItem(AItemBase* Item) const;
#pragma endregion
};
