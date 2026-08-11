#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "TaserGun.generated.h"

class USceneComponent;

/**
 * ATaserGun
 * 전투 아이템 - 테이저건. 좌클릭(OnUse) 시 총구에서 Line Trace를 발사해
 * 맞은 캐릭터(NPC/플레이어)를 일정 시간 기절(State.Stunned) 상태로 만든다.
 * 전기 이펙트(VFX)는 블루프린트에서 BlueprintImplementableEvent로 처리.
 */
UCLASS()
class TEAMPROJECT_MOU_API ATaserGun : public AItemBase
{
	GENERATED_BODY()

public:
	ATaserGun();

#pragma region [TASER] 컴포넌트
	// 총구 지점. BP에서 메시의 총구 위치로 옮겨두면 VFX가 여기서 시작한다.
	// (StaticMesh라 소켓이 없어 별도 컴포넌트로 총구 위치를 잡음)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Taser")
	TObjectPtr<USceneComponent> MuzzlePoint;
#pragma endregion

#pragma region [TASER] 설정값
	// 트레이스 사거리 (cm) - 카메라 시점 기준 전방
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Taser")
	float TraceDistance = 1500.0f;

	// 맞은 대상의 기절 지속 시간 (초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Taser")
	float StunDuration = 3.0f;
#pragma endregion

#pragma region [TASER] 사용/발사
	// [TASER-001] 좌클릭: 사용 횟수 차감 후 트레이스 발사
	virtual void OnUse_Implementation() override;
#pragma endregion

#pragma region [TASER] 연출 훅 (Blueprint VFX)
	// [TASER-005] 발사 이펙트 훅 (전기 줄기 등). 시작/끝 지점 전달, 모든 클라 재생
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireEffect(FVector Start, FVector End, bool bHit);

	// 블루프린트에서 실제 나이아가라/케이블 VFX를 붙이는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Taser|FX")
	void OnFireEffect(FVector Start, FVector End, bool bHit);
#pragma endregion

private:
#pragma region [TASER] 내부 로직
	// [TASER-002] 서버에서 실제 트레이스 + 기절 부여 (권한 처리)
	UFUNCTION(Server, Reliable)
	void ServerFire();

	// [TASER-003] 총구 위치/방향으로 Line Trace, 맞은 캐릭터에 기절 부여
	void PerformTrace();

	// [TASER-004] 대상 캐릭터에 기절 태그 부여 + 타이머로 해제 예약
	void ApplyStun(AActor* HitActor);
#pragma endregion
};
