#pragma once

#include "CoreMinimal.h"
#include "Base/CharacterBase.h"
#include "MainCharacter.generated.h"

class UInteractionComponent;
class UCarryingComponent;
class UInputAction;
class AItemBase;

UCLASS()
class TEAMPROJECT_MOU_API AMainCharacter : public ACharacterBase
{
	GENERATED_BODY()

public:
	AMainCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// 상호작용 컴포넌트 반환
	UFUNCTION(BlueprintCallable, Category = "Components")
	UInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	// 운반 컴포넌트 반환
	UFUNCTION(BlueprintCallable, Category = "Components")
	UCarryingComponent* GetCarryingComponent() const { return CarryingComponent; }

	// 현재 달리기(Sprint) 중인지 여부 반환
	UFUNCTION(BlueprintCallable, Category = "Player|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	// ---------------------------------------------------------
	// [이동 조작 오버라이드] - ACharacterBase 상태 판정 연동
	// ---------------------------------------------------------

	virtual void DoMove(float Right, float Forward) override;
	virtual void DoJumpStart() override;

protected:
	// 상호작용 탐색 컴포넌트 (F키)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	// 물품 잡기/던지기 컴포넌트 (E/Q키)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCarryingComponent> CarryingComponent;

	// ---------------------------------------------------------
	// [입력 액션 (Enhanced Input Action)]
	// ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrabOrDropAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ThrowAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> UseAction;

	// ---------------------------------------------------------
	// [이모트(감정표현) 시스템]
	// ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> EmoteToggleAction;

	// 얼굴 동적 머티리얼 인스턴스 배열 (초기화 시 눈, 입 등을 찾아 자동 할당)
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> FaceMaterialInstances;

	// 감정 인덱스를 변경하는 머티리얼 파라미터 이름 (머티리얼에 적힌 이름과 정확히 일치해야 함)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emote")
	FName EmotionParameterName = FName("Emotion index"); 

	// 감정 색상을 변경하는 머티리얼 파라미터 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emote")
	FName EmotionColorParameterName = FName("Emission Color");

	// 이모트 퀵슬롯 UI 호출 이벤트 (블루프린트에서 위젯 띄우기 용도)
	UFUNCTION(BlueprintImplementableEvent, Category = "Emote")
	void OpenEmoteUI();

	// 얼굴 표정과 색상을 즉시 변경합니다.
	UFUNCTION(BlueprintCallable, Category = "Emote")
	void SetEmotion(int32 EmotionIndex, FLinearColor EmoteColor = FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));

	// 표정과 색상을 변경하고 애니메이션(몽타주)을 재생합니다.
	UFUNCTION(BlueprintCallable, Category = "Emote")
	void PlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor = FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));

	UFUNCTION(Server, Reliable)
	void ServerPlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayEmote(class UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor);

	// 이모트 재생이 끝났을 때 표정을 초기화(Index 0)하는 콜백
	UFUNCTION()
	void OnEmoteMontageEnded(class UAnimMontage* Montage, bool bInterrupted);

	// 현재 재생 중인 이모트 몽타주 캐싱 (이동 시 취소 용도)
	UPROPERTY(Transient)
	TObjectPtr<class UAnimMontage> CurrentEmoteMontage;

	// ---------------------------------------------------------
	// [스태미나 및 달리기 파라미터]
	// ---------------------------------------------------------

	// 일반 걷기 속도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Movement")
	float NormalWalkSpeed = 500.0f;

	// 달리기 속도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Movement")
	float SprintWalkSpeed = 800.0f;

	// 초당 스태미나 소모량 (달리기 시)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float StaminaDrainRate = 15.0f;

	// 초당 스태미나 회복량 (달리기 멈춤 시)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float StaminaRegenRate = 10.0f;

	// 스태미나 0 도달 시 달리기 쿨다운 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Attributes")
	float ExhaustionCooldownDuration = 2.0f;

private:
	// 입력 핸들러 함수들
	void OnInteract();
	void OnGrabOrDrop();
	void OnThrow();
	void OnSprintStart();
	void OnSprintEnd();
	void OnJumpStartInput();
	void OnJumpEndInput();
	void OnEmoteToggle();
	void OnUse();

	// 달리기 동기화를 위한 서버 RPC
	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bSprint);

	// 스태미나 처리 프라이빗 메서드
	void UpdateStamina(float DeltaTime);

	// 현재 달리기 조작 입력 상태
	UPROPERTY(Replicated)
	bool bIsSprinting = false;

	// 스태미나 고갈에 따른 쿨다운 딜레이 진행 타이머
	float CurrentExhaustionTimer = 0.0f;
};
