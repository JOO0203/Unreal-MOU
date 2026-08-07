#include "Player/MainCharacter.h"
#include "Components/InteractionComponent.h"
#include "Components/CarryingComponent.h"
#include "Components/StatusComponent.h"
#include "Base/BaseAttributeSet.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/StaticMeshComponent.h"
#include "Base/ItemBase.h"
#include "Net/UnrealNetwork.h"

AMainCharacter::AMainCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 상호작용 및 물품 운반 컴포넌트 추가
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	CarryingComponent = CreateDefaultSubobject<UCarryingComponent>(TEXT("CarryingComponent"));
}

void AMainCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 눈(얼굴)과 입 메시에 적용된 동적 머티리얼 인스턴스(DMI) 생성 및 캐싱
	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* SM : StaticMeshes)
	{
		// 블루프린트에서 만든 컴포넌트 이름에 'Eye' 또는 'Mouth'가 포함되어 있는지 확인
		if (SM->GetName().Contains(TEXT("Eye")) || SM->GetName().Contains(TEXT("Mouth")))
		{
			if (UMaterialInstanceDynamic* DMI = SM->CreateAndSetMaterialInstanceDynamic(0))
			{
				FaceMaterialInstances.Add(DMI);
			}
		}
	}
}

void AMainCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 매 프레임 스태미나 소모 및 회복 처리
	UpdateStamina(DeltaTime);
}

void AMainCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMainCharacter, bIsSprinting);
}

void AMainCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent)
	{
		return;
	}

	// F키: 상호작용
	if (InteractAction)
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AMainCharacter::OnInteract);
	}

	// E키: 물건 잡기 / 놓기
	if (GrabOrDropAction)
	{
		EnhancedInputComponent->BindAction(GrabOrDropAction, ETriggerEvent::Started, this, &AMainCharacter::OnGrabOrDrop);
	}

	// Q키: 물건 던지기
	if (ThrowAction)
	{
		EnhancedInputComponent->BindAction(ThrowAction, ETriggerEvent::Started, this, &AMainCharacter::OnThrow);
	}

	// Shift키: 달리기
	if (SprintAction)
	{
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AMainCharacter::OnSprintStart);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMainCharacter::OnSprintEnd);
	}
	
	// 좌클릭: 아이템 사용
	if (UseAction)
	{
		EnhancedInputComponent->BindAction(UseAction, ETriggerEvent::Started, this, &AMainCharacter::OnUse);
	}

	// Space키: 점프 (ATeamProject_MOUCharacter 상속 JumpAction 사용)
	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AMainCharacter::OnJumpStartInput);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMainCharacter::OnJumpEndInput);
	}

	// ` 키 (백틱): 이모트 퀵슬롯 UI 토글
	if (EmoteToggleAction)
	{
		EnhancedInputComponent->BindAction(EmoteToggleAction, ETriggerEvent::Started, this, &AMainCharacter::OnEmoteToggle);
	}
}

void AMainCharacter::DoMove(float Right, float Forward)
{
	// 기절, 잡힘, 넉백 등 이동 불가능한 상태 체크
	if (!CanMove())
	{
		return;
	}

	// [이모트 취소 로직] 이동 키 입력이 들어왔고 현재 재생 중인 이모트가 있다면 즉시 취소
	if ((FMath::Abs(Right) > 0.1f || FMath::Abs(Forward) > 0.1f) && CurrentEmoteMontage != nullptr)
	{
		if (GetMesh() && GetMesh()->GetAnimInstance())
		{
			// 몽타주 정지 시 OnEmoteMontageEnded가 호출되어 표정도 자동으로 0으로 돌아감
			GetMesh()->GetAnimInstance()->Montage_Stop(0.2f, CurrentEmoteMontage);
		}
	}

	Super::DoMove(Right, Forward);
}

void AMainCharacter::DoJumpStart()
{
	// 이동 불가능 상태일 때 점프 차단
	if (!CanMove())
	{
		return;
	}

	Super::DoJumpStart();
}

void AMainCharacter::OnInteract()
{
	// 행동 불가능(기절/잡힘) 상태 체크
	if (!CanAct())
	{
		return;
	}

	if (InteractionComponent)
	{
		InteractionComponent->PerformInteraction();
	}
}

void AMainCharacter::OnGrabOrDrop()
{
	if (!CanAct())
	{
		return;
	}

	if (CarryingComponent)
	{
		CarryingComponent->GrabOrDrop();
	}
}

void AMainCharacter::OnThrow()
{
	if (!CanAct())
	{
		return;
	}

	if (CarryingComponent)
	{
		CarryingComponent->Throw();
	}
}

void AMainCharacter::OnSprintStart()
{
	// StatusComponent의 CanSprint() 판단 (기절/스태미나 고갈 시 불가)
	if (StatusComponent && !StatusComponent->CanSprint())
	{
		bIsSprinting = false;
		return;
	}

	// 클라이언트 측 예측 (즉시 달리기 적용)
	bIsSprinting = true;
	GetCharacterMovement()->MaxWalkSpeed = SprintWalkSpeed;

	// 서버로 달리기 요청 전송
	if (!HasAuthority())
	{
		ServerSetSprinting(true);
	}
}

void AMainCharacter::OnSprintEnd()
{
	// 클라이언트 측 즉시 달리기 중지 적용
	bIsSprinting = false;
	GetCharacterMovement()->MaxWalkSpeed = NormalWalkSpeed;

	// 서버로 달리기 중지 요청 전송
	if (!HasAuthority())
	{
		ServerSetSprinting(false);
	}
}

void AMainCharacter::ServerSetSprinting_Implementation(bool bSprint)
{
	if (bSprint)
	{
		if (StatusComponent && !StatusComponent->CanSprint())
		{
			bIsSprinting = false;
			return; // 달리기 불가
		}
		bIsSprinting = true;
		GetCharacterMovement()->MaxWalkSpeed = SprintWalkSpeed;
	}
	else
	{
		bIsSprinting = false;
		GetCharacterMovement()->MaxWalkSpeed = NormalWalkSpeed;
	}
}

void AMainCharacter::OnJumpStartInput()
{
	DoJumpStart();
}

void AMainCharacter::OnJumpEndInput()
{
	DoJumpEnd();
}

void AMainCharacter::OnUse()
{
	if (!CanAct())
	{
		return;
	}

	// 현재 들고있는 택배(CarryingComponent)가 있다면 일반 아이템 사용 불가 (손이 비어있어야 함)
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		UE_LOG(LogTemp, Warning, TEXT("물건을 들고 있어서 아이템을 사용할 수 없습니다!"));
		return;
	}

	// TODO: 장착 중인 아이템 사용 로직 (인벤토리 시스템 연동 시 구현)
	UE_LOG(LogTemp, Log, TEXT("아이템 사용 시도 (구현 필요)"));
}

void AMainCharacter::UpdateStamina(float DeltaTime)
{
	if (!BaseAttribute)
	{
		return;
	}

	float CurrentStamina = BaseAttribute->GetStemina();
	float MaxStamina = BaseAttribute->GetMaxStemina();

	// 고갈(Exhausted) 쿨다운 타이머 처리
	if (CurrentExhaustionTimer > 0.0f)
	{
		CurrentExhaustionTimer -= DeltaTime;
		if (CurrentExhaustionTimer <= 0.0f)
		{
			// 쿨다운 종료 시 Exhausted 상태 태그 제거 (서버 권한)
			if (HasAuthority() && StatusComponent)
			{
				static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Exhausted"), false);
				StatusComponent->RemoveStatusTag(ExhaustedTag);
			}
		}
	}

	// 실제 스태미나 증감 연산은 서버에서만 수행 (AttributeSet의 복제(Replication)를 통해 클라이언트에 동기화됨)
	if (!HasAuthority())
	{
		return;
	}

	// 캐릭터가 실제로 이동 중인지 체크
	bool bIsMoving = GetVelocity().SizeSquared2D() > 10.0f;

	if (bIsSprinting && bIsMoving && CanMove())
	{
		// 달리기 중 스태미나 감속
		float NewStamina = FMath::Clamp(CurrentStamina - (StaminaDrainRate * DeltaTime), 0.0f, MaxStamina);
		BaseAttribute->SetStemina(NewStamina);

		// 스태미나 0 도달 시 처리
		if (NewStamina <= 0.0f)
		{
			OnSprintEnd();

			// State.Exhausted 태그 부여 및 쿨다운 시작
			if (StatusComponent)
			{
				static const FGameplayTag ExhaustedTag = FGameplayTag::RequestGameplayTag(FName("State.Exhausted"), false);
				StatusComponent->AddStatusTag(ExhaustedTag);
			}

			CurrentExhaustionTimer = ExhaustionCooldownDuration;
		}
	}
	else
	{
		// 달리기를 하지 않거나 멈췄을 때 스태미나 회복
		if (CurrentStamina < MaxStamina && CurrentExhaustionTimer <= 0.0f)
		{
			float NewStamina = FMath::Clamp(CurrentStamina + (StaminaRegenRate * DeltaTime), 0.0f, MaxStamina);
			BaseAttribute->SetStemina(NewStamina);
		}
	}
}

// ---------------------------------------------------------
// [이모트 시스템 구현]
// ---------------------------------------------------------

void AMainCharacter::OnEmoteToggle()
{
	if (!CanAct())
	{
		return;
	}

	// 블루프린트에서 UI 위젯을 띄우는 이벤트를 호출
	OpenEmoteUI();
}

void AMainCharacter::SetEmotion(int32 EmotionIndex, FLinearColor EmoteColor)
{
	for (UMaterialInstanceDynamic* DMI : FaceMaterialInstances)
	{
		if (DMI)
		{
			DMI->SetScalarParameterValue(EmotionParameterName, static_cast<float>(EmotionIndex));
			DMI->SetVectorParameterValue(EmotionColorParameterName, EmoteColor);
		}
	}
}

void AMainCharacter::PlayEmote(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (!EmoteMontage || !GetMesh() || !GetMesh()->GetAnimInstance())
	{
		return;
	}

	// [이모트 사용 시 자동 Drop] 물건을 들고 있는 상태라면 먼저 내려놓음
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	if (!HasAuthority())
	{
		ServerPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
	}
	else
	{
		MulticastPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
	}
}

void AMainCharacter::ServerPlayEmote_Implementation(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	MulticastPlayEmote(EmoteMontage, EmotionIndex, EmoteColor);
}

void AMainCharacter::MulticastPlayEmote_Implementation(UAnimMontage* EmoteMontage, int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (!EmoteMontage || !GetMesh() || !GetMesh()->GetAnimInstance())
	{
		return;
	}

	// 1. 선택한 감정 표정과 색상으로 얼굴 머티리얼 변경
	SetEmotion(EmotionIndex, EmoteColor);

	// 2. 이모트 몽타주 재생
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	AnimInstance->Montage_Play(EmoteMontage);
	CurrentEmoteMontage = EmoteMontage;

	// 3. 몽타주가 끝날 때를 감지하기 위해 델리게이트 바인딩
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AMainCharacter::OnEmoteMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, EmoteMontage);
}

void AMainCharacter::OnEmoteMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// 방금 끝난 몽타주가 이모트 몽타주인지 확인
	if (Montage == CurrentEmoteMontage)
	{
		// 이모트가 끝났거나 중단(이동으로 인한 취소)되었으므로 기본 표정(0)과 기본 색상으로 복귀
		SetEmotion(0, FLinearColor(0.0f, 0.623294f, 1.0f, 1.0f));
		CurrentEmoteMontage = nullptr;
	}
}
