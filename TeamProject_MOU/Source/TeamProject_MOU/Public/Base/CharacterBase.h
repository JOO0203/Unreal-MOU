// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TeamProject_MOUCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayAbilitySpecHandle.h"
#include "CharacterBase.generated.h"

class AController;
class UAbilitySystemComponent;
class UBaseAttributeSet;
class UGameplayAbility;
struct FOnAttributeChangeData; // GAS attribute 값이 변경시 전달되는 데이터타입, 변경 감지용 콜백으로 사용

UCLASS()
class TEAMPROJECT_MOU_API ACharacterBase : public ATeamProject_MOUCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()



public:
	ACharacterBase();

	/*어빌리티 시스템 컴포넌트*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/*능력치 어빌리티 클래스*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TArray<TObjectPtr<UAttributeSet>>  BaseAttributeSet;

	/*공용 능력치*/
	UPROPERTY(EditAnywhere, Category = "Attributes")
	TObjectPtr<UBaseAttributeSet> BaseAttribute;

	/*캐릭터가 보유할수있는 스킬 배열*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GASGamePlayAbility")
	TArray<TSubclassOf<UGameplayAbility>> InitalAbilities;

	// Health 또는 MaxHealth가 변경되었을 때 Blueprint로 알려주는 이벤트
	// 플레이어에서 이 이벤트를 받아 HP UI를 갱신
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnHealthUpdated(float CurrentHealth, float MaxHealth);

	//stemina 또는 MaxStemina가 변경되었을 때 Blueprint로 알려주는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSteminaupdated(float CurrentStemina, float MaxStemina);

	//speed 또는 MaxSpeed가 변경되었을 때 Blueprint로 알려주는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSpeedUpdated (float CurrentSpeed, float MaxSpeed);

protected:
	/*멀티플레이 모드 GAS*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode AscReplicationMode = EGameplayEffectReplicationMode::Mixed;

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	// AbilitySystemComponent에 Attribute 변경 감지 델리게이트를 등록
	// 이 함수를 호출시 Health, MP 같은 Attribute가 변경될 때 자동으로 콜백 함수가 실행
	virtual void BindAttributeChangeDelegates();

	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);
	void HandleSteminaChanged(const FOnAttributeChangeData& Data);
	void HandleMaxSteminaChanged(const FOnAttributeChangeData& Data);
	void HandleMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data);

	// BeginPlay, PossessedBy, OnRep_PlayerState 등 여러 타이밍에서 바인딩을 시도를 막기위해서
	// 같은 Attribute 변경 콜백이 중복 등록되지 않도록 막는 플래그
	bool AttributeDelegatesBound = false;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	FGameplayAbilitySpecHandle InitializeAbility(TSubclassOf<UGameplayAbility> AbilityToGet, int32 AbilityLevel);
	void InitializeAbilityMulti(TArray<TSubclassOf<UGameplayAbility>> AbilityToAcquire, int32 AbilityLevel);
};
