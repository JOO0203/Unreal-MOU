// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ProjectGameStateBase.generated.h"

/**
 * 
 */
UCLASS()
class TEAMPROJECT_MOU_API AProjectGameStateBase : public AGameStateBase
{
	GENERATED_BODY()
	
public:
	AProjectGameStateBase();

	// ==============================================
	// Gold
	// ==============================================

	// 현재 팀이 보유한 골드
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Gold, Category = "Economy")
	int32 Gold;

	// 골드 추가
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void AddGold(int32 Amount);

	// 골드 사용
	// 골드가 부족하면 false 반환
	UFUNCTION(BlueprintCallable, Category = "Economy")
	bool SpendGold(int32 Amount);

	// 현재 골드로 구매 가능한지 확인
	UFUNCTION(BlueprintPure, Category = "Economy")
	bool CanAfford(int32 Amount) const;

	// 저장 데이터에서 복원할 때 사용
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void SetGold(int32 NewGold);

	// ==============================================
	// Reputation
	// ==============================================

	// 팀 평판
	// -100 ~ 100
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Reputation, Category = "Reputation")
	int32 Reputation;

	// 평판 증감
	UFUNCTION(BlueprintCallable, Category = "Reputation")
	void AddReputation(int32 Amount);

	// 저장 데이터에서 복원할 때 사용
	UFUNCTION(BlueprintCallable, Category = "Reputation")
	void SetReputation(int32 NewReputation);

	// ==============================================
	// UI / BluePrintEvent
	// ==============================================
	
	// 골드가 변경되었을 때 BP에서 UI 갱신용
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy")
	void OnGoldUpdated(int32 NewGold);

	// 평판이 변경되었을 때 BP에서 UI 갱신용
	UFUNCTION(BlueprintImplementableEvent, Category = "Reputation")
	void OnReputationUpdated(int32 NewReputation);

protected:
	UFUNCTION()
	void OnRep_Gold();

	UFUNCTION()
	void OnRep_Reputation();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
