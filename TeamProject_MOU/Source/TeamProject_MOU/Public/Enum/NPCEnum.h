#pragma once

#include "CoreMinimal.h"
#include "NPCEnum.generated.h"

/* NPC 종류 */
UENUM(BlueprintType)
enum class ENpcType : uint8
{
	Normal		UMETA(DisplayName = "Nomal", ToolTip = "기본 NPC"),
	Push		UMETA(DisplayName = "Push", ToolTip = "플레이어를 미는 NPC"),
	Shop		UMETA(DisplayName = "Shop", ToolTip = "상점 NPC"),
	Quest		UMETA(DisplayName = "Quest", ToolTip = "퀘스트 NPC"),
	Stun		UMETA(DisplayName = "Stun", ToolTip = "플레이어를 기절시키는 NPC"),
	Snatch		UMETA(DisplayName = "Snatch", ToolTip = "플레이어의 물건을 뺏어서 도망가는 NPC"),
	Hold		UMETA(DisplayName = "Hold", ToolTip = "플레이어를 드는 NPC"),
	Blocking	UMETA(DisplayName = "Blocking", ToolTip = "플레이어의 길을 막는 NPC")
};

/* NPC 시작 상태*/
UENUM(BlueprintType)
enum class ENPCStartState : uint8
{
    Stay,
    Patrol
};

/*NPC 행동 후 정책*/
UENUM(BlueprintType)
enum class ENPCAfterActionPolicy : uint8
{
    /*타깃이 계속 보이면 다시 추적/공격 반복*/
    RepeatWhileTargetVisible,
    /*행동 후 정지*/
    OneShotThenStay,
    /*행동 후 추적*/
    OneShotThenTracking,
    /*행동 후 정찰*/
    OneShotThenPatrol
};

/*NPC 타겟을 잃었을 때 정책*/
UENUM(BlueprintType)
enum class ENPCLostTargetPolicy : uint8
{
    /*행동 후 대기*/
    ReturnToStay,
    /*행동 후 정찰*/
    ReturnToPatrol,
    /*행동 후 원래 위치로*/
    ReturnHome
};