#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/InteractableInterface.h"
#include "ItemBase.generated.h"

class UStaticMeshComponent;
class UTexture2D;

UCLASS()
class TEAMPROJECT_MOU_API AItemBase : public AActor, public IInteractableInterface
{
	GENERATED_BODY()
	
public:	
	AItemBase();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [컴포넌트]
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// ---------------------------------------------------------
	// [기본 아이템 데이터]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Data")
	FText ItemName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Data")
	TObjectPtr<UTexture2D> ItemIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Data")
	float ItemWeight = 1.0f;

	// ---------------------------------------------------------
	// [상태 관리 데이터]
	// ---------------------------------------------------------
	// 사용 횟수 (주로 일반 아이템용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Status")
	int32 MaxUseCount = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Item|Status")
	int32 CurrentUseCount = 1;

	// 내구도 (주로 파괴 가능한 아이템 및 택배용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Item|Status")
	float MaxDurability = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Item|Status")
	float CurrentDurability = 100.0f;

	// 마지막으로 이 아이템을 소유했던 액터 (평판 추적 등)
	UPROPERTY(BlueprintReadOnly, Category = "Item|Tracking")
	TObjectPtr<AActor> LastOwner;

	// ---------------------------------------------------------
	// [상호작용 인터페이스 구현 (IInteractableInterface)]
	// ---------------------------------------------------------
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;

	// ---------------------------------------------------------
	// [핵심 행동 함수]
	// ---------------------------------------------------------
	
	// E키로 바닥에서 집어들 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void PickUp(AActor* Picker);
	virtual void PickUp_Implementation(AActor* Picker);

	// E키로 바닥에 내려놓을 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void Drop(FVector DropLocation);
	virtual void Drop_Implementation(FVector DropLocation);

	// Q키로 던질 때 호출
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void Throw(FVector ThrowVelocity);
	virtual void Throw_Implementation(FVector ThrowVelocity);

	// 물리 동기화를 위한 멀티캐스트 함수들
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPickUp(AActor* Picker);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDrop(FVector DropLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastThrow(FVector ThrowVelocity);

	// 좌클릭으로 아이템을 사용할 때 호출 (택배의 경우 기능을 비움)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnUse();
	virtual void OnUse_Implementation();

	// 손에 장착될 때 (인벤토리에서 활성화될 때)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnEquipped(AActor* Equipper);
	virtual void OnEquipped_Implementation(AActor* Equipper);

	// 손에서 해제될 때 (인벤토리로 들어갈 때)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Action")
	void OnUnequipped(AActor* Equipper);
	virtual void OnUnequipped_Implementation(AActor* Equipper);
};
