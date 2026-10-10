#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ThrowChargeWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * UThrowChargeWidget
 * 물건 던지기 차징 진행률(0.0 ~ 1.0)을 시각화하는 ProgressBar UI 위젯입니다.
 */
UCLASS()
class TEAMPROJECT_MOU_API UThrowChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UThrowChargeWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;

	/** 차징 상태 변경 (시작 시 표시, 종료 시 숨김) */
	UFUNCTION(BlueprintCallable, Category = "UI|Throw")
	void SetChargingState(bool bIsCharging);

	/** 차징 진행률(0.0 ~ 1.0) 업데이트 */
	UFUNCTION(BlueprintCallable, Category = "UI|Throw")
	void UpdateCharge(float ChargeRatio);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Charge;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ChargePercent;

	/** 최소 힘 게이지 색상 (초록/하늘색 계열) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Throw|Color")
	FLinearColor MinChargeColor = FLinearColor(0.1f, 0.8f, 0.3f, 1.0f);

	/** 최대 힘 게이지 색상 (주황/빨강 계열) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Throw|Color")
	FLinearColor MaxChargeColor = FLinearColor(1.0f, 0.3f, 0.1f, 1.0f);

	/** 블루프린트에서 차징 상태 변경 연출 처리 */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Throw")
	void OnChargingStateChanged(bool bIsCharging);

	/** 블루프린트에서 차징 수치 변경 연출 처리 */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Throw")
	void OnChargeUpdated(float ChargeRatio);
};
