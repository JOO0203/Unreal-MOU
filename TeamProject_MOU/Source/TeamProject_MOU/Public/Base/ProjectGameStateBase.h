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

	// ���� ���� ������ ���
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Gold, Category = "Economy")
	int32 Gold;

	// ��� �߰�
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void AddGold(int32 Amount);

	// ��� ���
	// ��尡 �����ϸ� false ��ȯ
	UFUNCTION(BlueprintCallable, Category = "Economy")
	bool SpendGold(int32 Amount);

	// ���� ���� ���� �������� Ȯ��
	UFUNCTION(BlueprintPure, Category = "Economy")
	bool CanAfford(int32 Amount) const;

	// ���� �����Ϳ��� ������ �� ���
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void SetGold(int32 NewGold);

	// ==============================================
	// Reputation
	// ==============================================

	// �� ����
	// -100 ~ 100
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Reputation, Category = "Reputation")
	int32 Reputation;

	// ���� ����
	UFUNCTION(BlueprintCallable, Category = "Reputation")
	void AddReputation(int32 Amount);

	// ���� �����Ϳ��� ������ �� ���
	UFUNCTION(BlueprintCallable, Category = "Reputation")
	void SetReputation(int32 NewReputation);

	// ==============================================
	// UI / BluePrintEvent
	// ==============================================
	
	// ��尡 ����Ǿ��� �� BP���� UI ���ſ�
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy")
	void OnGoldUpdated(int32 NewGold);

	// ������ ����Ǿ��� �� BP���� UI ���ſ�
	UFUNCTION(BlueprintImplementableEvent, Category = "Reputation")
	void OnReputationUpdated(int32 NewReputation);

	// ==============================================
	// Debt
	// ==============================================

	// ���� ��
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	int32 InitialDebt;

	// ���� ���ƾ� �ϴ� ��
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentDebt, Category = "Economy|Debt")
	int32 CurrentDebt;

	// �� ���� ����� �⺻ ������
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	int32 BaseDebtIncrease;

	// �� ���� �ӵ� ������
	// ���� �������� �Ĺ� �� ���� �ӵ��� ������
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Debt")
	float DebtGrowthDivisor;

	// ���� �� ��° ��ȯ ȸ������
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DebtCycle, Category = "Economy|Debt")
	int32 DebtCycle;

	// ���� ��ȯ ȸ�� ���� �� ������ ���(�̹��� �� �����ϳ�)
	UFUNCTION(BlueprintPure, Category = "Economy|Debt")
	int32 CalculateDebtIncrease() const;

	// ���� ��ȯ ȸ�� ���� ���� �� ���(���������� �����ؼ� ���� �� �Ѿ��� �󸶳�)
	UFUNCTION(BlueprintPure, Category = "Economy|Debt")
	int32 CalculateNextDebt() const;

	// ���� ���� ��ȯ
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	bool PayDebt();

	// �� ���� ���� ����
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	void SetCurrentDebt(int32 NewDebt);

	// �� ��ȯ ȸ���� ���� ����
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	void SetDebtCycle(int32 NewDebtCycle);

	// ���� ��ȯ ������ Ȯ���ϰ� ��ȯ ó�� ��� ��ȯ
	UFUNCTION(BlueprintCallable, Category = "Economy|Debt")
	EDebtProcessResult ProcessDebtDeadline();

	// �� ����� UI ���ſ�
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy|Debt")
	void OnDebtUpdated(int32 NewDebt);

	// �� ��ȯ ȸ�� ���� �� UI ���ſ�
	UFUNCTION(BlueprintImplementableEvent, Category = "Economy|Debt")
	void OnDebtCycleUpdated(int32 NewDebtCycle);

	// ==============================================
	// Economy Time
	// ==============================================

	// ���� ���� �ý��ۿ��� ����� HalfDay
	// �ӽ� �ð����̸� �� �̵� 1ȸ�� 1 ����
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EconomyCurrentHalfDay, Category = "Economy|Time")
	int32 EconomyCurrentHalfDay;

	// �� ��ȯ ����
	// ���� �ӽ� �ð� ���� 14 HalfDay = 7��
	// ���� ���� �ϼ� �ý��� �߰� �� �ش� �ð� ���ذ� ����
	UPROPERTY(BlueprintReadOnly, Category = "Economy|Time")
	int32 DebtPeriodHalfDay;

	// ���� �ð� 1 HalfDay ����
	UFUNCTION(BlueprintCallable, Category = "Economy|Time")
	void AdvanceEconomyHalfDay();

	// ���� ���� HalfDay ��ȯ
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetEconomyCurrentHalfDay() const;

	// ���� ������ ������
	UFUNCTION(BlueprintCallable, Category = "Economy|Time")
	void SetEconomyCurrentHalfDay(int32 NewHalfDay);

	// ���� ��ȯ ȸ�� ���� ���� ��ȯ ������ ���
	// DebtCycle * DebtPeriodHalfDay
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetNextDebtDueHalfDay() const;

	// ���� ���� �ð��� ���� ��ȯ ���� �̻����� Ȯ��
	// true�̸� �� ��ȯ ó�� �ʿ�
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	bool IsDebtDue() const;

	// ���� �� ��ȯ���� ���� HalfDay ��ȯ
	// �̹� ���ѿ� ������ ��� 0 ��ȯ
	UFUNCTION(BlueprintPure, Category = "Economy|Time")
	int32 GetRemainingDebtHalfDay() const;

	// ���� �ð� ���� �� BP/UI ���ſ�
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

