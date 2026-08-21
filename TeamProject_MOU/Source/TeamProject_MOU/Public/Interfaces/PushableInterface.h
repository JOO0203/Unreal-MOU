#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PushableInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable)
class UPushableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 밀기 기능을 지원하는 객체(플레이어, 택배, 퍼즐 등)들이 상속받는 인터페이스
 */
class TEAMPROJECT_MOU_API IPushableInterface
{
	GENERATED_BODY()

public:
	/**
	 * 대상의 밀기 저항값(무게 등)을 반환합니다.
	 * 반환값이 0.0 이하이면 저항을 무시합니다(예: 플레이어).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Push")
	float GetPushResistance() const;

	/**
	 * 대상을 미는 함수입니다.
	 * @param Pusher 미는 주체 (플레이어 등)
	 * @param PushDirection 밀어내는 방향
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction|Push")
	void Push(AActor* Pusher, FVector PushDirection);
};
