// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Economy/EconomyTypes.h"
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

	// ==============================================
	// Debt
	// ==============================================

	// 최초 빚
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	int32 InitialDebt;

	// 현재 갚아야 하는 빚
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentDebt, Category = "Economy|Debt")
	int32 CurrentDebt;

	// 빚 증가 계산의 기본 증가량
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	int32 BaseDebtIncrease;

	// 빚 증가 속도 조절값
	// 값이 작을수록 후반 빚 증가 속도가 빨라짐
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	float DebtGrowthDivisor;

	// 현재 몇 번째 상환 회차인지
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DebtCycle, Category = "Economy|Debt")
	int32 DebtCycle;

	// 현재 상환 회차 기준 빚 증가량 계산(이번에 얼마 증가하냐)
	UFUNCTION(BlueprintPure, Category = "Economy|Debt")
	int32 CalculateDebtIncrease() const;

	// 현재 상환 회차 기준 다음 빚 계산(증가량까지 포함해서 다음 빚 총액이 얼마냐)
	UFUNCTION(BlueprintPure, Category = "Economy|Debt")
	int32 CalculateNextDebt() const;

	// 현재 빚을 상환
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	bool PayDebt();

	// 빚 값을 직접 설정
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	void SetCurrentDebt(int32 NewDebt);

	// 빚 상환 회차를 직접 설정
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	void SetDebtCycle(int32 NewDebtCycle);

	// 현재 상환 기한을 확인하고 상환 처리 결과 반환
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	EDebtProcessResult ProcessDebtDeadline();

	// 빚 변경시 UI 갱신용
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy|Debt")
	void OnDebtUpdated(int32 NewDebt);

	// 빚 상환 회차 변경 시 UI 갱신용
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy|Debt")
	void OnDebtCycleUpdated(int32 NewDebtCycle);

	// ==============================================
	// Economy Time
	// ==============================================

	// 현재 경제 시스템에서 경과한 HalfDay
	// 임시 시간값이며 맵 이동 1회당 1 증가
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EconomyCurrentHalfDay, Category = "Economy|Time")
	int32 EconomyCurrentHalfDay;

	// 빚 상환 간격
	// 현재 임시 시간 기준 14 HalfDay = 7일
	// 추후 실제 일수 시스템 추가 시 해당 시간 기준과 연결
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Time")
	int32 DebtPeriodHalfDay;

	// 경제 시간 1 HalfDay 진행
	UFUNCTION(BlueprintCallable, Category = "Economy|Time")
	void AdvanceEconomyHalfDay();

	// 현재 경제 HalfDay 반환
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetEconomyCurrentHalfDay() const;

	// 저장 데이터 복원용
	UFUNCTION(BlueprintCallable, Category = "Economy|Time")
	void SetEconomyCurrentHalfDay(int32 NewHalfDay);

	// 현재 상환 회차 기준 다음 상환 시점을 계산
	// DebtCycle * DebtPeriodHalfDay
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetNextDebtDueHalfDay() const;

	// 현재 경제 시간이 다음 상환 시점 이상인지 확인
	// true이면 빚 상환 처리 필요
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	bool IsDebtDue() const;

	// 다음 빚 상환까지 남은 HalfDay 반환
	// 이미 기한에 도달한 경우 0 반환
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetRemainingDebtHalfDay() const;

	// 경제 시간 변경 시 BP/UI 갱신용
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy|Time")
	void OnEconomyHalfDayUpdated(int32 NewHalfDay);

protected:
	UFUNCTION()
	void OnRep_Gold();

	UFUNCTION()
	void OnRep_Reputation();

	UFUNCTION()
	void OnRep_CurrentDebt();

	UFUNCTION()
	void OnRep_DebtCycle();

	UFUNCTION()
	void OnRep_EconomyCurrentHalfDay();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
