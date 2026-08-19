#pragma once

#include "CoreMinimal.h"
#include "Item/ConsumableItemBase.h"
#include "GameplayTagContainer.h"
#include "PotionItem.generated.h"

class UGameplayEffect;

/**
 * APotionItem
 * 회복/버프 계열 소비 아이템. 좌클릭 시 자기 자신(SelfOnly)에게 효과 적용.
 *
 * "다용성"은 클래스를 여러 개 만들지 않고 GameplayEffect 목록으로 해결한다:
 *   - 체력 회복  = Health를 올리는 Instant/Periodic GE
 *   - 속도 증가  = MoveSpeed/MaxMoveSpeed를 올리는 Duration GE
 *   - 경량화/소지량 증가 = MaxWeight를 올리는(또는 CurrentWeight를 낮추는) Duration GE
 *   - 상태이상 제거 = TagsToRemove로 StatusComponent 태그 제거 (즉시)
 * 지속시간/원복/복제는 전부 GAS가 처리한다. 버프 태그(Buff.*)는 각 GE가 Granted Tags로 부여.
 *
 * 실제 포션 종류(힐 포션, 속도 포션 등)는 BP에서 EffectsToApply/TagsToRemove만 바꿔 만든다.
 */
UCLASS()
class TEAMPROJECT_MOU_API APotionItem : public AConsumableItemBase
{
	GENERATED_BODY()

public:
	APotionItem();

protected:
#pragma region [POTION] 설정값
	// 사용 시 대상 ASC에 적용할 GameplayEffect 목록 (회복/힘/속도/경량화 모두 여기)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Potion")
	TArray<TSubclassOf<UGameplayEffect>> EffectsToApply;

	// GameplayEffect 적용 레벨 (GE 안에서 레벨로 수치를 스케일링할 때 사용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Potion")
	float EffectLevel = 1.0f;

	// 사용 시 제거할 상태이상 태그 (예: State.Slowed, State.Exhausted, State.CC.Stuned)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Potion")
	FGameplayTagContainer TagsToRemove;
#pragma endregion

#pragma region [POTION] 효과 적용
	// [POTION-001] 소비 효과: 대상 ASC에 GE 적용 + 상태이상 태그 제거 (서버에서만 호출됨)
	virtual void ApplyEffect_Implementation() override;
#pragma endregion
};
