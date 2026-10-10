#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MOU_CharacterStatusHUD.generated.h"

class UImage;
class UProgressBar;
class UTexture2D;
class UWidget;
class AMainCharacter;
class UThrowChargeWidget;

UENUM(BlueprintType)
enum class ECharacterStatusState : uint8
{
	Happy,
	OK,
	Warning,
	Critical,
	Offline
};

/**
 * Character Status HUD
 * Handles HP, Stamina circular bars and dynamically changes center portrait
 */
UCLASS()
class TEAMPROJECT_MOU_API UMOU_CharacterStatusHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Set Target HP (0.0 ~ 1.0) for interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetTargetHP(float NewHPPercent);

	// Set Target Stamina (0.0 ~ 1.0) for interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void SetTargetStamina(float NewStaminaPercent);

	// Immediately set values without interpolation
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void ForceUpdateStatus(float NewHPPercent, float NewStaminaPercent);

	// Bind status updates to a specific character (e.g. for spectating)
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void BindToCharacter(AMainCharacter* InCharacter);

	UFUNCTION(BlueprintPure, Category = "UI|Status")
	AMainCharacter* GetBoundCharacter() const { return BoundCharacter.Get(); }

	// 던지기 차징 UI 갱신 (차징 시작/진행/종료/취소)
	UFUNCTION(BlueprintCallable, Category = "UI|Status")
	void UpdateThrowCharge(bool bIsCharging, float ChargeRatio);

	// 내부의 ThrowChargeWidget 인스턴스 반환
	UFUNCTION(BlueprintPure, Category = "UI|Status")
	UThrowChargeWidget* GetThrowChargeWidget();

protected:
	// -- UI Components --

	UPROPERTY(meta = (BindWidget))
	UProgressBar* ProgressBar_HP;

	UPROPERTY(meta = (BindWidget))
	UProgressBar* ProgressBar_Stamina;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_CenterPortrait;

	// UMG에 배치된 던지기 차징 위젯
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Status")
	TObjectPtr<UThrowChargeWidget> ThrowChargeWidget;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "UI|Status")
	TObjectPtr<UThrowChargeWidget> ThrowChargeWiget;

	// 블루프린트에서 던지기 차징 연출을 직접 제어할 수 있는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Status")
	void OnThrowChargeUpdated(bool bIsCharging, float ChargeRatio);

	// -- Portrait Textures --

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Happy;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_OK;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Warning;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Critical;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Portrait")
	UTexture2D* Tex_Offline;

	// -- Interpolation Setting --

	UPROPERTY(EditDefaultsOnly, Category = "UI|Status|Interpolation")
	float CatchUpInterpSpeed = 5.0f;

private:
	float TargetHPPercent = 1.0f;
	float CurrentHPPercent = 1.0f;

	float TargetStaminaPercent = 1.0f;
	float CurrentStaminaPercent = 1.0f;

	ECharacterStatusState CurrentState = ECharacterStatusState::Happy;

	UPROPERTY()
	TWeakObjectPtr<AMainCharacter> BoundCharacter;

	void UpdatePortraitState(float InCurrentHP);
	void SetPortraitTextureByState(ECharacterStatusState NewState);
};
