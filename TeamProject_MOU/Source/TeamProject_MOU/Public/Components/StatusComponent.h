#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "StatusEffect/StatusEffectDataAsset.h"
#include "StatusComponent.generated.h"

class UAbilitySystemComponent;
class UStatusEffectAbilitySetDataAsset;

/*
 * GAS 기반 상태이상 컴포넌트입니다.
 * 상태 태그의 실제 보관과 복제는 AbilitySystemComponent가 담당하고,
 * 이 컴포넌트는 상태이상 GA 자동 지급과 공통 상태 조회를 제공합니다.
 */
UCLASS(ClassGroup = (StatusEffect), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_MOU_API UStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStatusComponent();

	/* 데이터 에셋에 등록된 상태이상 GA를 Owner ASC에 중복 없이 지급합니다. */
	UFUNCTION(BlueprintCallable, Category = "Status Effect")
	void GrantStatusEffectAbilities();

	/* ASC가 현재 해당 상태 태그를 보유하고 있는지 확인합니다. */
	UFUNCTION(BlueprintPure, Category = "Status Effect")
	bool HasStatusTag(FGameplayTag Tag) const;

	/* 현재 ASC가 가진 모든 명시적 Gameplay Tag를 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "Status Effect")
	FGameplayTagContainer GetActiveStatusTags() const;

	/* 기존 코드 호환용입니다. 새 상태이상은 GA/GE에서 태그를 부여하는 방식을 사용하세요. */
	UFUNCTION(BlueprintCallable, Category = "Status Effect", meta = (DeprecatedFunction, DeprecationMessage = "상태이상 태그는 GA/GE에서 부여하세요."))
	void AddStatusTag(FGameplayTag Tag);

	/* 기존 코드 호환용입니다. 새 상태이상은 GA/GE에서 태그를 제거하는 방식을 사용하세요. */
	UFUNCTION(BlueprintCallable, Category = "Status Effect", meta = (DeprecatedFunction, DeprecationMessage = "상태이상 태그는 GA/GE에서 제거하세요."))
	void RemoveStatusTag(FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "Status Effect|State")
	bool CanMove() const;

	UFUNCTION(BlueprintPure, Category = "Status Effect|State")
	bool CanAct() const;

	UFUNCTION(BlueprintPure, Category = "Status Effect|State")
	bool CanSprint() const;

	UFUNCTION(BlueprintPure, Category = "Status Effect|State")
	bool CanCarry() const;

	UFUNCTION(BlueprintPure, Category = "Status Effect")
	UAbilitySystemComponent* GetOwnerAbilitySystemComponent() const { return AbilitySystemComponent; }

	/* 유닛의 Ability Set에서 요청한 종류의 상태이상 설정을 찾습니다. */
	UFUNCTION(BlueprintPure, Category = "Status Effect")
	UStatusEffectDataAsset* GetStatusEffectData(EStatusEffectType EffectType) const;

protected:
	virtual void BeginPlay() override;

	/* Owner에게 자동 지급할 상태이상 GA 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	TObjectPtr<UStatusEffectAbilitySetDataAsset> AbilitySetData;

	/* Owner가 가진 AbilitySystemComponent 캐시 */
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/* 이 컴포넌트가 새로 지급한 Ability의 핸들 */
	UPROPERTY(Transient)
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

private:
	UAbilitySystemComponent* FindOwnerAbilitySystemComponent() const;
};
