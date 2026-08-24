#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopBase.generated.h"

class AItemBase;
class AItemSpawner;
class UDataTable;
class USceneComponent;
class UStaticMeshComponent;

/**
 * AShopBase
 * 상점(배달 트럭)의 C++ 베이스. BP_Shop의 부모 클래스로 사용한다.
 *
 * 역할:
 *   1) 진열 지점 관리 - BP에서 ItemAnchorRoot 밑에 Scene Component를 자식으로 놓으면
 *      그게 진열 슬롯이 된다. 트럭 짐칸 형상에 맞춰 위치를 눈으로 조정 가능.
 *   2) 아이템 스폰 - DT_Item(FItemSpawnRow)에서 지정한 6~8개 행을 각 슬롯에 스폰한다.
 *      스폰 로직 자체는 AItemSpawner의 SpawnItemAt을 재사용한다(중복 구현 방지).
 *
 * 스폰은 서버 권한에서만 한다(멀티 대응, 스폰 액터는 클라로 복제됨).
 * 구매(F키)는 플레이어 쪽에서 처리하므로 여기서는 다루지 않는다.
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

	// 진열 슬롯들의 부모. BP에서 이 밑에 Scene Component를 자식으로 놓으면 그게 진열 지점이 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> ItemAnchorRoot;
#pragma endregion

#pragma region [SHOP] 설정값
	// 아이템 데이터 테이블 (행 구조 = FItemSpawnRow). 상점에서 스폰할 아이템의 원본 데이터.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TObjectPtr<UDataTable> ItemTable;

	// 진열할 아이템 행 이름 목록 (6~8개). ItemTable의 행 이름을 넣는다.
	// 슬롯보다 많으면 슬롯 개수만큼만, 적으면 목록 개수만큼만 스폰한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TArray<FName> ShopItemRows;

	// BeginPlay에 진열 아이템을 자동 스폰할지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bAutoSpawnOnBeginPlay = true;
#pragma endregion

#pragma region [SHOP] 내부 상태
	// 스폰 로직 재사용을 위한 스포너 인스턴스(서버에서 BeginPlay에 1개 생성).
	// 표 데이터 주입 + Deferred 스폰 흐름을 AItemSpawner와 공유하기 위함.
	UPROPERTY()
	TObjectPtr<AItemSpawner> InternalSpawner;

	// 이번에 진열된 아이템들(스폰 결과). 재진열/정리 시 사용.
	UPROPERTY()
	TArray<TObjectPtr<AItemBase>> DisplayedItems;
#pragma endregion

public:
#pragma region [SHOP] 스폰 함수
	// [SHOP-001] 진열 슬롯 트랜스폼 목록을 반환 (ItemAnchorRoot의 자식 Scene Component들)
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void GetSlotTransforms(TArray<FTransform>& OutTransforms) const;

	// [SHOP-002] ShopItemRows를 각 슬롯에 스폰 (서버 권한에서만). 기존 진열은 먼저 정리한다.
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void SpawnShopItems();

	// [SHOP-003] 진열된 아이템 전부 제거 (재진열 전 정리용)
	UFUNCTION(BlueprintCallable, Category = "Shop")
	void ClearShopItems();
#pragma endregion
};
