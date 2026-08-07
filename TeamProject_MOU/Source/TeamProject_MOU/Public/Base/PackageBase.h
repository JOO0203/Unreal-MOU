#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "Interfaces/PushableInterface.h"
#include "PackageBase.generated.h"

UENUM(BlueprintType)
enum class EPackageType : uint8
{
	Normal UMETA(DisplayName = "일반 물품"),
	Heavy UMETA(DisplayName = "무거운 물품"),
	Fragile UMETA(DisplayName = "깨지기 쉬운 물품"),
	Perishable UMETA(DisplayName = "상하기 쉬운 물품"),
	Dangerous UMETA(DisplayName = "위험 물품")
};

UCLASS()
class TEAMPROJECT_MOU_API APackageBase : public AItemBase, public IPushableInterface
{
	GENERATED_BODY()
	
public:
	APackageBase();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// ---------------------------------------------------------
	// [택배 기본 데이터]
	// ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	int32 BaseValue = 100;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	EPackageType PackageType = EPackageType::Normal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Package|Data")
	float MaxSpoilTime = 0.0f; // 0이면 상하지 않음

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Package|Status")
	float CurrentSpoilTime = 0.0f;

	// ---------------------------------------------------------
	// [협동 운반 데이터]
	// ---------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Package|Coop")
	TArray<TObjectPtr<AActor>> CurrentCarriers;

	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void AddCarrier(AActor* Carrier);

	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void RemoveCarrier(AActor* Carrier);

	// 운반자들의 스피드 배율을 계산하고 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "Package|Coop")
	void UpdateCarriersSpeedModifier();

	// 각 운반자에게 분배되어 적용된 무게를 추적하는 맵
	UPROPERTY(Transient)
	TMap<AActor*, float> AppliedWeightMap;

	// ---------------------------------------------------------
	// [아이템 베이스 오버라이드]
	// ---------------------------------------------------------
	
	// 택배는 일반 아이템처럼 우클릭/좌클릭 사용이 불가능함
	virtual void OnUse_Implementation() override;

	// ---------------------------------------------------------
	// [밀기 인터페이스 구현 (IPushableInterface)]
	// ---------------------------------------------------------
	virtual float GetPushResistance_Implementation() const override;
	virtual void Push_Implementation(AActor* Pusher, FVector PushDirection) override;
};
