#include "Economy/EconomyDebtProcessor.h"
#include "Base/ProjectGameStateBase.h"

EDebtProcessResult FEconomyDebtProcessor::Process(
	AProjectGameStateBase* GameState)
{
	// 유효한 GameState인지 확인
	if (!IsValid(GameState))
	{
		return EDebtProcessResult::NotDue;
	}

	// 실제 경제 상태 변경은 서버에서만 처리
	if (!GameState->HasAuthority())
	{
		return EDebtProcessResult::NotDue;
	}

	// 아직 상환 기한이 아니면 처리하지 않음
	if (!GameState->IsDebtDue())
	{
		return EDebtProcessResult::NotDue;
	}

	// 상환 기한 도달 후 정상적으로 빚을 갚은 경우
	if (GameState->PayDebt())
	{
		return EDebtProcessResult::Paid;
	}

	// 상환 기한에 도달했지만 Gold 부족
	return EDebtProcessResult::Failed;
}