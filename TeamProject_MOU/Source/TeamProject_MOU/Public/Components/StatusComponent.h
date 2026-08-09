#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StatusComponent.generated.h"

class UAbilitySystemComponent;

// 상태 태그 변경 시 호출되는 델리게이트 (팀원의 UI 및 NPC AI에서 구독 가능)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStatusTagChanged, FGameplayTag, Tag, bool, bAdded);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStatusComponent();

protected:
	virtual void BeginPlay() override;

public:
	// ---------------------------------------------------------
	// [상태 태그 조작 함수] - 플레이어 및 NPC 공통 사용
	// ---------------------------------------------------------

	// 특정 상태 태그 추가 (예: State.Stunned, State.Held, State.Slowed 등)
	UFUNCTION(BlueprintCallable, Category = "Status")
	void AddStatusTag(FGameplayTag Tag);

	// 특정 상태 태그 제거
	UFUNCTION(BlueprintCallable, Category = "Status")
	void RemoveStatusTag(FGameplayTag Tag);

	// 특정 상태 태그를 가지고 있는지 확인
	UFUNCTION(BlueprintCallable, Category = "Status")
	bool HasStatusTag(FGameplayTag Tag) const;

	// 현재 부여된 모든 상태 태그 반환
	UFUNCTION(BlueprintCallable, Category = "Status")
	FGameplayTagContainer GetActiveStatusTags() const { return ActiveStatusTags; }

	// ---------------------------------------------------------
	// [행동 가능 여부 판단 함수] - AI 및 컨트롤러에서 상태 체크용
	// ---------------------------------------------------------

	// 이동 가능 상태인지 확인 (기절, 잡힘, 넉백 상태가 아닐 때 true)
	UFUNCTION(BlueprintCallable, Category = "Status")
	bool CanMove() const;

	// 행동(공격, 상호작용, 아이템 사용 등) 가능 상태인지 확인
	UFUNCTION(BlueprintCallable, Category = "Status")
	bool CanAct() const;

	// 달리기 가능 상태인지 확인
	UFUNCTION(BlueprintCallable, Category = "Status")
	bool CanSprint() const;

	// 물품 운반/잡기 가능 상태인지 확인 (팔 손상 시 한계 적용)
	UFUNCTION(BlueprintCallable, Category = "Status")
	bool CanCarry() const;

public:
	// 상태 태그 변경 알림 델리게이트 (NPC AI StateTree 및 플레이어 UI 연동용)
	UPROPERTY(BlueprintAssignable, Category = "Status|Events")
	FOnStatusTagChanged OnStatusTagChanged;

protected:
	// 현재 캐릭터에게 적용된 상태 태그 컨테이너
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ActiveStatusTags, Category = "Status")
	FGameplayTagContainer ActiveStatusTags;

	UFUNCTION()
	void OnRep_ActiveStatusTags(const FGameplayTagContainer& OldTags);

	// 소유자 Actor의 AbilitySystemComponent 참조
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
};
