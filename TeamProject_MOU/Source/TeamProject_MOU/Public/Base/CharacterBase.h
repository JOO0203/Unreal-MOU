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
class UStatusComponent;
struct FOnAttributeChangeData;

/**
 * ACharacterBase
 * 플레이어 및 방해 NPC/적대 세력의 최상위 공통 베이스 캐릭터 클래스.
 * GAS(Gameplay Ability System) 및 공통 상태 관리(StatusComponent)를 내장합니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API ACharacterBase : public ATeamProject_MOUCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACharacterBase();

	// ---------------------------------------------------------
	// [GAS 및 상태 관리 컴포넌트]
	// ---------------------------------------------------------

	// 어빌리티 시스템 컴포넌트 (GAS 핵심 기능 관리)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	// 어트리뷰트 세트 배열 (GAS 어트리뷰트 보관함)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TArray<TObjectPtr<UAttributeSet>> BaseAttributeSet;

	// 기본 어트리뷰트 세트 (체력, 스태미나, 이동속도 속성 소유)
	UPROPERTY(EditAnywhere, Category = "Attributes")
	TObjectPtr<UBaseAttributeSet> BaseAttribute;

	// 상태 이상 및 디버프 관리 컴포넌트 (플레이어 및 NPC 공통 사용)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status")
	TObjectPtr<UStatusComponent> StatusComponent;

	// 캐릭터 생성 시 기본 부여할 Gameplay Ability 스킬 배열
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GASGamePlayAbility")
	TArray<TSubclassOf<UGameplayAbility>> InitalAbilities;

	// ---------------------------------------------------------
	// [공통 캐릭터 상태 판정 함수] - AIController 및 PlayerController 공유
	// ---------------------------------------------------------

	// 캐릭터가 현재 이동 가능한 상태인지 확인 (기절/잡힘/넉백 상태 시 false)
	UFUNCTION(BlueprintCallable, Category = "Status")
	virtual bool CanMove() const;

	// 캐릭터가 현재 행동(공격, 상호작용, 스킬) 가능한 상태인지 확인
	UFUNCTION(BlueprintCallable, Category = "Status")
	virtual bool CanAct() const;

	// ---------------------------------------------------------
	// [Blueprint 이벤트] - UI 및 연출 업데이트용 델리게이트 알림
	// ---------------------------------------------------------

	// 체력(Health/MaxHealth) 변경 시 호출되는 이벤트 (UI 및 AI 업데이트용)
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnHealthUpdated(float CurrentHealth, float MaxHealth);

	// 스태미나(Stemina/MaxStemina) 변경 시 호출되는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSteminaupdated(float CurrentStemina, float MaxStemina);

	// 이동속도(MoveSpeed/MaxMoveSpeed) 변경 시 호출되는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Attributes")
	void OnSpeedUpdated(float CurrentSpeed, float MaxSpeed);

protected:
	// GAS 리플리케이션 모드 (멀티플레이 동기화 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AbilitySystem")
	EGameplayEffectReplicationMode AscReplicationMode = EGameplayEffectReplicationMode::Mixed;

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	// GAS Attribute 변경 감지 델리게이트 바인딩 함수
	virtual void BindAttributeChangeDelegates();

	// 체력 변경 콜백 핸들러
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);

	// 스태미나 변경 콜백 핸들러
	void HandleSteminaChanged(const FOnAttributeChangeData& Data);
	void HandleMaxSteminaChanged(const FOnAttributeChangeData& Data);

	// 이동속도 변경 콜백 핸들러
	void HandleMoveSpeedChanged(const FOnAttributeChangeData& Data);
	void HandleMaxMoveSpeedChanged(const FOnAttributeChangeData& Data);

	// 델리게이트 중복 바인딩 방지 플래그
	bool AttributeDelegatesBound = false;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// IAbilitySystemInterface 구현: AbilitySystemComponent 반환
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// StatusComponent 반환
	UStatusComponent* GetStatusComponent() const { return StatusComponent; }

	// 단일 Gameplay Ability 부여 함수
	FGameplayAbilitySpecHandle InitializeAbility(TSubclassOf<UGameplayAbility> AbilityToGet, int32 AbilityLevel);

	// 다수 Gameplay Ability 일괄 부여 함수
	void InitializeAbilityMulti(TArray<TSubclassOf<UGameplayAbility>> AbilityToAcquire, int32 AbilityLevel);
};
