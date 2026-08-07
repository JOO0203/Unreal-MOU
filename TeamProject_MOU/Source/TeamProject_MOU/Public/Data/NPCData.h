// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Enum/NPCEnum.h"
#include "NPCData.generated.h"

class UBehaviorTree;
class UGameplayAbility;
class UGameplayEffect;
class USkeletalMesh;
class UAnimInstance;

UCLASS()
class TEAMPROJECT_MOU_API UNPCData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	//UNPCData();


	/*NPC 행동 유형*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Identity")
	ENpcType EnemyType = ENpcType::Normal;

	/*사용할 Behavior Tree */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI")
	TObjectPtr<UBehaviorTree> BehaviorTree = nullptr;

	/* 시야 감지 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI",
		meta = (ClampMin = "0.0", Units = "cm"))
	float SightRadius = 1500.0f;

	/* 타겟을 놓치는 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI",
		meta = (ClampMin = "0.0", Units = "cm"))
	float LoseSightRadius = 2000.0f;

	/* 주변 시야각의 절반 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float PeripheralVisionHalfAngle = 70.0f;


	/* 평상시 이동속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Movement",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkSpeed = 250.0f;

	/* 추적 또는 도주 시 이동속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Movement",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float RunSpeed = 500.0f;


	/* 공통 BT에서 실행할 대표 Ability 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Ability",
		meta = (Categories = "Ability.Enemy"))
	FGameplayTag PrimaryAbilityTag;

	/* NPC 생성 시 부여할 Gameplay Ability 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Ability")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/* NPC 생성 시 적용할 초기 Gameplay Effect */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Ability")
	TArray<TSubclassOf<UGameplayEffect>> InitialEffects;


	/* 기본 행동 또는 공격이 가능한 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ActionRange = 200.0f;

	/* 행동을 다시 시도하기 전 기본 대기시간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat",
		meta = (ClampMin = "0.0", Units = "s"))
	float ActionInterval = 1.5f;


	/* 적에게 사용할 스켈레탈 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Appearance")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/* 적에게 사용할 애니메이션 블루프린트 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Appearance")
	TSubclassOf<UAnimInstance> AnimInstanceClass;
	
};
