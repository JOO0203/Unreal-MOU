#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "CarryingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCarriedStateChanged, AActor*, CarriedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnThrowChargeChanged, bool, bIsCharging, float, ChargeRatio);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UCarryingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarryingComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

public:
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void GrabOrDrop();

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void Throw(const FVector& CustomThrowDir = FVector::ZeroVector);

	/** 지정된 힘(Force)으로 투척 실행 */
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void ThrowWithForce(const FVector& CustomThrowDir, float InThrowForce);

	UFUNCTION(Server, Reliable)
	void ServerGrabOrDrop();

	UFUNCTION(Server, Reliable)
	void ServerThrow(const FVector& InThrowDir);

	UFUNCTION(Server, Reliable)
	void ServerThrowWithForce(const FVector& InThrowDir, float InThrowForce);

	// ---------------------------------------------------------
	// [던지기 힘 조절(차징) 시스템]
	// ---------------------------------------------------------

	/** 던지기 차징 시작 (우클릭 누를 때) */
	UFUNCTION(BlueprintCallable, Category = "Carrying|Throw")
	void StartThrowCharge();

	/** 매 프레임 차징 시간 누적 및 게이지 갱신 */
	UFUNCTION(BlueprintCallable, Category = "Carrying|Throw")
	void UpdateThrowCharge(float DeltaTime);

	/** 던지기 차징 완료 및 투척 (우클릭 뗄 때) */
	UFUNCTION(BlueprintCallable, Category = "Carrying|Throw")
	void FinishThrowCharge(const FVector& CustomThrowDir = FVector::ZeroVector);

	/** 차징 취소 (피격, 그로기, 드랍 등) */
	UFUNCTION(BlueprintCallable, Category = "Carrying|Throw")
	void CancelThrowCharge();

	/** 현재 차징 진행률 반환 (0.0 ~ 1.0) */
	UFUNCTION(BlueprintPure, Category = "Carrying|Throw")
	float GetThrowChargeRatio() const;

	/** 현재 차징된 투척 힘(cm/s) 반환 (MinThrowForce ~ MaxThrowForce 사이 보간값) */
	UFUNCTION(BlueprintPure, Category = "Carrying|Throw")
	float GetCurrentThrowForce() const;

	/** 차징 진행 중 여부 */
	UFUNCTION(BlueprintPure, Category = "Carrying|Throw")
	bool IsChargingThrow() const { return bIsChargingThrow; }

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	bool IsCarrying() const { return CarriedActor != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	bool IsCarryingCharacter() const;

	UFUNCTION(BlueprintCallable, Category = "Carrying")
	AActor* GetCarriedActor() const { return CarriedActor; }

	// 인벤토리 등에서 특정 아이템을 손에 강제로 쥐어줄 때 사용
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void EquipItem(AActor* ItemToEquip);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastEquipItem(AActor* ItemToEquip);

	// 손에 든 물건을 인벤토리에 넣거나 파괴할 때 손을 비우기 위해 사용
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void ClearCarriedItem();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastClearCarriedItem();

	// 손에 든 물건과 인벤토리 슬롯 아이템의 총 무게를 계산하여 AttributeSet에 동기화
	UFUNCTION(BlueprintCallable, Category = "Carrying")
	void UpdateCharacterTotalWeight();

	UPROPERTY(BlueprintAssignable, Category = "Carrying")
	FOnCarriedStateChanged OnCarriedStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Carrying|Throw")
	FOnThrowChargeChanged OnThrowChargeChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying|Throw", meta = (ClampMin = "100.0"))
	float DefaultThrowForce = 1300.0f;

	/** 던지기 차징 시 최소 힘 (cm/s, 짧게 클릭 시 적용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying|Throw", meta = (ClampMin = "100.0"))
	float MinThrowForce = 600.0f;

	/** 던지기 차징 시 최대 힘 (cm/s, 풀 차징 시 적용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying|Throw", meta = (ClampMin = "100.0"))
	float MaxThrowForce = 2200.0f;

	/** 최소 힘에서 최대 힘까지 도달하는 데 걸리는 차징 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying|Throw", meta = (ClampMin = "0.1", Units = "s"))
	float MaxChargeTime = 1.2f;

	// 던질 때 자연스러운 포물선을 그리도록 상향(Z축)으로 띄워주는 보정 비율 (기본값: 0.28f, 약 16도 상향)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying|Throw", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThrowUpwardBias = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrying")
	FName CarrySocketName = TEXT("CarrySocket");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Carrying|Throw")
	bool bIsChargingThrow = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Carrying|Throw")
	float CurrentThrowChargeTime = 0.0f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_CarriedActor)
	TObjectPtr<AActor> CarriedActor;

	UFUNCTION()
	void OnRep_CarriedActor(AActor* OldCarriedActor);

	UPROPERTY(Transient)
	struct FGameplayAbilitySpecHandle ActiveCarryAbilitySpecHandle;
};
