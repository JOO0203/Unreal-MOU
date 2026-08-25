// Fill out your copyright notice in the Description page of Project Settings.


#include "Base/ProjectGameStateBase.h"

#include "Economy/EconomyDebtProcessor.h"
#include "Net/UnrealNetwork.h"

AProjectGameStateBase::AProjectGameStateBase()
{
	Gold = 0;
	Reputation = 0;

	InitialDebt = 500;
	CurrentDebt = InitialDebt;

	BaseDebtIncrease = 250;
	DebtGrowthDivisor = 8.0f;

	DebtCycle = 1;

	EconomyCurrentHalfDay = 0;
	DebtPeriodHalfDay = 14;
}

// ==============================================
// Gold
// ==============================================

void AProjectGameStateBase::AddGold(int32 Amount)
{
	// Ŭ���̾�Ʈ�� ���Ƿ� ��带 �������� ���ϵ��� ��.
	// GameState�� ���� Gold ���� ������ ����
	if (!HasAuthority())
	{
		return;
	}

	// 0 �Ǵ� ���� ���� AddGold�� ���� ����.
    // ��� ���Ҵ� SpendGold�� ����ϵ��� ������ �и�.
	if (Amount <= 0)
	{
		return;
	}

	// ���� �� ��� ����
	Gold += Amount;


	// ���� �������� Gold ���� ����� Blueprint�� �˷���.
	// HUD ���� UI ���ſ� ����� �� ����.
	OnGoldUpdated(Gold);
}

bool AProjectGameStateBase::SpendGold(int32 Amount)
{
	// ���������� ���� ��� ���� ���
	if (!HasAuthority())
	{
		return false;
	}

	// 0�̳� ���� �ݾ� ��� ����
	if (Amount <= 0)
	{
		return false;
	}

	// ���� ��尡 Amount���� ������ ����/���� ����
	if (!CanAfford(Amount))
	{
		return false;
	}

	// ���� ��� ����
	Gold -= Amount;

	// UI � ����� Gold ����
	OnGoldUpdated(Gold);


	return true;
}

// ���� �����ʰ� ��� �ִ��� �˻��ϴ� �Լ�
bool AProjectGameStateBase::CanAfford(int32 Amount) const
{
	return Amount > 0 && Gold >= Amount;
}

void AProjectGameStateBase::SetGold(int32 NewGold)
{
	if (!HasAuthority())
	{
		return;
	}

	Gold = FMath::Max(0, NewGold);

	OnGoldUpdated(Gold);
}

// ==============================================
// Reputation
// ==============================================

void AProjectGameStateBase::AddReputation(int32 Amount)
{
	if (!HasAuthority())
	{
		return;
	}

	Reputation = FMath::Clamp(Reputation + Amount, -100, 100);

	OnReputationUpdated(Reputation);
}

void AProjectGameStateBase::SetReputation(int32 NewReputation)
{
	if (!HasAuthority())
	{
		return;
	}

	Reputation = FMath::Clamp(NewReputation, -100, 100);

	OnReputationUpdated(Reputation);
}

// ==============================================
// Debt
// ==============================================

//������ = BaseDebtIncrease �� (1 + ((DebtCycle - 1)�� / DebtGrowthDivisor))
int32 AProjectGameStateBase::CalculateDebtIncrease() const
{
	const float CycleValue = static_cast<float>(FMath::Max(0, DebtCycle - 1));

	const float SafeDivisor = FMath::Max(0.01f, DebtGrowthDivisor);

	const float GrowthFactor = 1.0f + (FMath::Square(CycleValue) / SafeDivisor);

	const float Increase = static_cast<float>(BaseDebtIncrease) * GrowthFactor;

	return FMath::RoundToInt(Increase);
}

// ���� �� + �̹� ������
int32 AProjectGameStateBase::CalculateNextDebt() const
{
	return CurrentDebt + CalculateDebtIncrease();
}

bool AProjectGameStateBase::PayDebt()
{
	if (!HasAuthority())
	{
		return false;
	}

	// ���� ���� ���� ��尡 �����ϸ� ��ȯ ����
	if (!SpendGold(CurrentDebt))
	{
		return false;
	}

	// ���� DebtCycle �������� ���� ���� ���� ���
	const int32 NextDebt = CalculateNextDebt();

	// ��ȯ ���� �� ���� ȸ���� ����
	DebtCycle++;
	OnDebtCycleUpdated(DebtCycle);

	// ���� ���� �� ����
	CurrentDebt = NextDebt;
	OnDebtUpdated(CurrentDebt);

	return true;
}

void AProjectGameStateBase::SetCurrentDebt(int32 NewDebt)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentDebt = FMath::Max(0, NewDebt);

	OnDebtUpdated(CurrentDebt);
}

void AProjectGameStateBase::SetDebtCycle(int32 NewDebtCycle)
{
	if (!HasAuthority())
	{
		return;
	}

	DebtCycle = FMath::Max(1, NewDebtCycle);

	OnDebtCycleUpdated(DebtCycle);
}

EDebtProcessResult AProjectGameStateBase::ProcessDebtDeadline()
{
	return FEconomyDebtProcessor::Process(this);
}

// ==============================================
// Economy Time
// ==============================================

// ���� �ð� 1 HalfDay ����
void AProjectGameStateBase::AdvanceEconomyHalfDay()
{
	if (!HasAuthority())
	{
		return;
	}

	EconomyCurrentHalfDay++;

	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// ���� ���� HalfDay ��ȯ
int32 AProjectGameStateBase::GetEconomyCurrentHalfDay() const
{
	return EconomyCurrentHalfDay;
}

// ���� ������ ������
void AProjectGameStateBase::SetEconomyCurrentHalfDay(int32 NewHalfDay)
{
	if (!HasAuthority())
	{
		return;
	}

	EconomyCurrentHalfDay = FMath::Max(0, NewHalfDay);

	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// ���� �� ��ȯ HalfDay ��ȯ
int32 AProjectGameStateBase::GetNextDebtDueHalfDay() const
{
	return DebtCycle * DebtPeriodHalfDay;
}

// ���� �� ��ȯ ���ѿ� �����ߴ��� Ȯ��
bool AProjectGameStateBase::IsDebtDue() const
{
	return EconomyCurrentHalfDay >= GetNextDebtDueHalfDay();
}

// �� ��ȯ���� ���� HalfDay
int32 AProjectGameStateBase::GetRemainingDebtHalfDay() const
{
	return FMath::Max(0,GetNextDebtDueHalfDay() - EconomyCurrentHalfDay);
}

// ==============================================
// RepNotify
// ==============================================

void AProjectGameStateBase::OnRep_Gold()
{
	OnGoldUpdated(Gold);
}

void AProjectGameStateBase::OnRep_Reputation()
{
	OnReputationUpdated(Reputation);
}
void AProjectGameStateBase::OnRep_CurrentDebt()
{
	OnDebtUpdated(CurrentDebt);
}

void AProjectGameStateBase::OnRep_DebtCycle()
{
	OnDebtCycleUpdated(DebtCycle);
}

void AProjectGameStateBase::OnRep_EconomyCurrentHalfDay()
{
	OnEconomyHalfDayUpdated(EconomyCurrentHalfDay);
}

// ==============================================
// Replication
// ==============================================

void AProjectGameStateBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProjectGameStateBase, Gold);
	DOREPLIFETIME(AProjectGameStateBase, Reputation);
	DOREPLIFETIME(AProjectGameStateBase, CurrentDebt);
	DOREPLIFETIME(AProjectGameStateBase, DebtCycle);
	DOREPLIFETIME(AProjectGameStateBase, EconomyCurrentHalfDay);
}


