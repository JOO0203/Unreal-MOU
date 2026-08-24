#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"

class AProjectGameStateBase;

// 빚 상환 기한 처리 전용 클래스
// 실제 경제 데이터는 GameState가 관리하고,
// 이 클래스는 상환 기한 판정 및 결과 반환만 담당
class TEAMPROJECT_MOU_API FEconomyDebtProcessor
{
public:

	// 현재 경제 시간을 기준으로 빚 상환 기한 처리
	static EDebtProcessResult Process(AProjectGameStateBase* GameState);
};