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
#include "Base/ItemBase.h"
#include "Base/PackageBase.h"
#include "Base/EventObjectBase.h"
#include "Components/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"

AMainCharacter::AMainCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 컴포넌트 추가
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	CarryingComponent = CreateDefaultSubobject<UCarryingComponent>(TEXT("CarryingComponent"));
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// 부활(살리기) 위젯 컴포넌트 생성 및 초기화
	ReviveWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("ReviveWidgetComponent"));
	ReviveWidgetComponent->SetupAttachment(RootComponent);
	ReviveWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen); // 화면 공간에 띄움
	ReviveWidgetComponent->SetDrawSize(FVector2D(200.0f, 50.0f));
	ReviveWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f)); // 머리 위쪽에 위치
	ReviveWidgetComponent->SetVisibility(false); // 기본적으로는 숨김

	// 시야(상호작용) Trace가 캐릭터를 인식할 수 있도록 Visibility 채널 Block 처리
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
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

	if (InteractionComponent)
	{
		InteractionComponent->OnFocusedInteractableChanged.AddDynamic(this, &AMainCharacter::OnFocusedActorChanged);
	}

	if (InventoryComponent)
	{
		InventoryComponent->OnEquipRequested.AddDynamic(this, &AMainCharacter::OnEquipRequested);
	}
}

void AMainCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 매 프레임 스태미나 소모 및 회복 처리
	UpdateStamina(DeltaTime);

	// [밀기 모드] 캐릭터의 실제 이동량만큼 이벤트 오브젝트를 함께 이동시킴
	// Simulated Proxy(다른 클라이언트 화면에 보이는 타 플레이어)는 서버의 복제를 따라가므로 이 로직을 실행하지 않음
	if (bIsPushingMode && CurrentPushedObject && (IsLocallyControlled() || HasAuthority()))
	{
		FVector CurrentLoc = GetActorLocation();
		FVector DeltaMove = CurrentLoc - LastCharacterLocation;
		DeltaMove.Z = 0.0f; // 상하 이동은 무시

		bool bCanMove = true;
		if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
		{
			bCanMove = EventObj->IsReadyToMove();
		}

		if (!bCanMove)
		{
			if (!DeltaMove.IsNearlyZero())
			{
				// 인원 부족: 애니메이션은 나오지만 캐릭터의 실제 위치는 이전으로 강제 고정
				SetActorLocation(LastCharacterLocation, false);
			}
			return;
		}

		if (bCanMove && !DeltaMove.IsNearlyZero())
		{
			// 0. 장애물(다른 상자나 벽) 감지 (바닥/경사면은 무시)
			FVector BoxCenter, BoxExtents;
			CurrentPushedObject->GetActorBounds(false, BoxCenter, BoxExtents);
			
			// 바닥 긁힘 방지를 위해 살짝 위(10cm)에서 전방으로 검사
			FVector TraceStart = BoxCenter + FVector(0, 0, 10.0f);
			FVector TraceEnd = TraceStart + DeltaMove;
			
			FCollisionQueryParams BoxParams;
			BoxParams.AddIgnoredActor(CurrentPushedObject);
			BoxParams.AddIgnoredActor(this);
			
			FHitResult BoxHit;
			// 상자보다 아주 살짝 작은 크기의 박스로 Sweep
			bool bHitObstacle = GetWorld()->SweepSingleByChannel(
				BoxHit, TraceStart, TraceEnd, 
				CurrentPushedObject->GetActorQuat(), ECC_Visibility, 
				FCollisionShape::MakeBox(BoxExtents * 0.95f), BoxParams);
				
			// 수직 장애물에 부딪혔을 때만 멈춤 (경사면 무시)
			if (bHitObstacle && FMath::Abs(BoxHit.ImpactNormal.Z) < 0.5f)
			{
				// 전방에 벽이나 다른 상자가 있으므로 캐릭터의 이동 취소
				SetActorLocation(LastCharacterLocation, false);
				return;
			}

			// 1. 경사면 이동 완벽 허용을 위해 Sweep 끄고 이동 적용 (장애물은 위에서 걸러냄)
			CurrentPushedObject->AddActorWorldOffset(DeltaMove, false);

			// 2. 바닥 감지 (낭떠러지 체크)
			FVector TraceStartFall = BoxCenter;
			FVector TraceEndFall = TraceStartFall - FVector(0, 0, BoxExtents.Z + 50.0f); // 상자 바닥에서 50cm 아래 검사
			FHitResult FallHit;
			FCollisionQueryParams FallParams;
			FallParams.AddIgnoredActor(CurrentPushedObject);
			FallParams.AddIgnoredActor(this);

			bool bHitFall = GetWorld()->LineTraceSingleByChannel(FallHit, TraceStartFall, TraceEndFall, ECC_Visibility, FallParams);
			if (!bHitFall)
			{
				// 바닥이 없으면 추락 처리 (서버에서만 판정하여 모두에게 동기화)
				if (HasAuthority())
				{
					if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
					{
						EventObj->MulticastFallOffLedge();
					}
				}
			}

			LastCharacterLocation = CurrentLoc;
		}
		else
		{
			LastCharacterLocation = CurrentLoc;
		}
	}

	// [2인 협동 운반] 서로를 바라보도록 캐릭터 회전 고정
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (APackageBase* HeavyPackage = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			if (HeavyPackage->PackageType == EPackageType::Heavy && HeavyPackage->CurrentCarriers.Num() >= 2)
			{
				if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
				{
					MoveComp->bOrientRotationToMovement = false; // 강제 회전 방지
				}
				
				// 택배(상대방)를 바라보도록 부드럽게 회전
				FVector DirToPackage = HeavyPackage->GetActorLocation() - GetActorLocation();
				DirToPackage.Z = 0.0f; // 상하 회전 방지
				if (!DirToPackage.IsNearlyZero())
				{
					SetActorRotation(FMath::RInterpTo(GetActorRotation(), DirToPackage.Rotation(), DeltaTime, 10.0f));
				}
			}
			else
			{
				// 혼자 들거나 다른 아이템을 들 때는 이동 방향 회전 원상복구
				if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
				{
					MoveComp->bOrientRotationToMovement = true;
				}
			}
		}
	}
	else if (!bIsPushingMode)
	{
		// 물건을 안 들고 있고 밀기 모드도 아닐 때만 원래 이동 방향 회전 복구
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->bOrientRotationToMovement = true;
		}
	}
}

void AMainCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMainCharacter, bIsSprinting);
	DOREPLIFETIME(AMainCharacter, bIsStunned);
	DOREPLIFETIME(AMainCharacter, bIsPushingMode);
	DOREPLIFETIME(AMainCharacter, DownCount);
	DOREPLIFETIME(AMainCharacter, bIsGroggy);
	DOREPLIFETIME(AMainCharacter, bIsDead);
	DOREPLIFETIME(AMainCharacter, bIsReviving);
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

	// 인벤토리 단축키 (1, 2, 3)
	if (Slot1Action)
	{
		EnhancedInputComponent->BindAction(Slot1Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot1);
	}
	if (Slot2Action)
	{
		EnhancedInputComponent->BindAction(Slot2Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot2);
	}
	if (Slot3Action)
	{
		EnhancedInputComponent->BindAction(Slot3Action, ETriggerEvent::Started, this, &AMainCharacter::OnSlot3);
	}
}

void AMainCharacter::DoMove(float Right, float Forward)
{
	// 기절, 잡힘, 넉백 등 이동 불가능한 상태 체크
	if (!CanMove())
	{
		return;
	}

	// 밀기 모드 중에는 좌우(Right) 이동 차단. 앞/뒤(W/S)만 허용
	if (bIsPushingMode)
	{
		Right = 0.0f;

		bool bCanMove = true;
		if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
		{
			bCanMove = EventObj->IsReadyToMove();
		}

		// 인원수 충족 여부에 관계없이 상시 이동 입력을 주어 헛발질(애니메이션)이 재생되게 함
		// 실제 이동 차단은 Tick에서 수행함
		if (FMath::Abs(Forward) > 0.0f)
		{
			AddMovementInput(LockedPushDirection, Forward);
		}
		
		// Super::DoMove()는 카메라 회전을 기준으로 방향을 잡으므로 생략
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

void AMainCharacter::OnFocusedActorChanged(AActor* NewFocusedActor)
{
	// 이전 포커스 액터가 아이템이면 UI 숨김
	if (AItemBase* PrevItem = Cast<AItemBase>(PreviousFocusedActor))
	{
		PrevItem->HideItemInfo();
	}

	// [요구사항] 물건을 잡고 있을 때는 UI 표시 안 함
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		PreviousFocusedActor = nullptr;
		return;
	}

	// 새 포커스 액터가 아이템이면 UI 표시
	if (AItemBase* NewItem = Cast<AItemBase>(NewFocusedActor))
	{
		NewItem->ShowItemInfo();
	}

	PreviousFocusedActor = NewFocusedActor;
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
	// 밀기 모드 중이라면 토글(해제) 처리
	if (bIsPushingMode)
	{
		StopPushMode();
		return;
	}

	// 행동 불가능(기절/잡힘) 상태 체크 (그로기/사망 상태에서도 대상 구도자는 살릴 수 있어야 함)
	// CanAct() 란 오직 자신의 상태만 보군도, 살리는 조작은 있어야 함
	// [수정] 자신이 그로기 또는 사망 상태일 때만 자신의 행동 차단
	if (bIsGroggy || bIsDead)
	{
		return;
	}

	if (InteractionComponent)
	{
		// 다른 캐릭터 살리기 케이스: 대상이 그로기 상태의 MainCharacter이면 Server RPC로 처리
		AActor* Focused = InteractionComponent->GetFocusedInteractable();
		if (AMainCharacter* GroggyTarget = Cast<AMainCharacter>(Focused))
		{
			if (GroggyTarget->bIsGroggy && !GroggyTarget->bIsDead)
			{
				ServerReviveTarget(GroggyTarget);
				return;
			}
		}

		// 일반 상호작용 (밀기, 풌스트, 레벨이동 등)
		if (!CanAct())
		{
			return;
		}
		InteractionComponent->PerformInteraction();
	}
}

void AMainCharacter::OnGrabOrDrop()
{
	if (!CanAct() || bIsPushingMode)
	{
		return;
	}

	if (CarryingComponent)
	{
		bool bWasCarrying = CarryingComponent->IsCarrying();
		CarryingComponent->GrabOrDrop();
		
		// 들고 있던 물건을 방금 내려놓았다면, 현재 바라보고 있는 대상의 UI를 다시 갱신(표시)
		if (bWasCarrying && !CarryingComponent->IsCarrying() && InteractionComponent)
		{
			OnFocusedActorChanged(InteractionComponent->GetFocusedInteractable());
		}
	}
}

void AMainCharacter::OnThrow()
{
	if (!CanAct() || bIsPushingMode)
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
	// 그로기 / 사망 / 부활중 상태에서는 달리기 차단
	if (!CanAct())
	{
		bIsSprinting = false;
		return;
	}

	// 밀기 모드 중 달리기 시도 시 자동 해제
	if (bIsPushingMode)
	{
		StopPushMode();
	}

	// StatusComponent의 CanSprint() 판단 (기절/스태미나 고갈 시 불가)
	if (StatusComponent && !StatusComponent->CanSprint())
	{
		bIsSprinting = false;
		return;
	}

	// [개선] 무거운 택배를 들고 있을 때는 달리기 불가
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		if (APackageBase* Package = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
		{
			if (Package->PackageType == EPackageType::Heavy)
			{
				bIsSprinting = false;
				return;
			}
		}
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

		// [개선] 무거운 택배를 들고 있을 때는 달리기 불가
		if (CarryingComponent && CarryingComponent->IsCarrying())
		{
			if (APackageBase* Package = Cast<APackageBase>(CarryingComponent->GetCarriedActor()))
			{
				if (Package->PackageType == EPackageType::Heavy)
				{
					bIsSprinting = false;
					return;
				}
			}
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
	// 밀기 모드 중 점프 시도 시 자동 해제
	if (bIsPushingMode)
	{
		StopPushMode();
	}

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
		if (AItemBase* HandItem = Cast<AItemBase>(CarryingComponent->GetCarriedActor()))
		{
			HandItem->OnUse();
		}
		return;
	

	// TODO: 장착 중인 아이템 사용 로직 (인벤토리 시스템 연동 시 구현)
	if (CarryingComponent && !CarryingComponent->IsCarrying())
	{
		UE_LOG(LogTemp, Warning, TEXT("물건이 없어서 사용할 수 없습니다!"));

	}
}

void AMainCharacter::OnSlot1()
{
	if (InventoryComponent) InventoryComponent->RequestSlotAction(0);
}

void AMainCharacter::OnSlot2()
{
	if (InventoryComponent) InventoryComponent->RequestSlotAction(1);
}

void AMainCharacter::OnSlot3()
{
	if (InventoryComponent) InventoryComponent->RequestSlotAction(2);
}

void AMainCharacter::OnEquipRequested(AItemBase* ItemToEquip)
{
	if (CarryingComponent)
	{
		if (ItemToEquip)
		{
			CarryingComponent->EquipItem(ItemToEquip);
		}
		else
		{
			CarryingComponent->ClearCarriedItem();
		}
	}
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

void AMainCharacter::ChangeEmotion(int32 EmotionIndex, FLinearColor EmoteColor)
{
	if (HasAuthority())
	{
		MulticastChangeEmotion(EmotionIndex, EmoteColor);
	}
	else
	{
		ServerChangeEmotion(EmotionIndex, EmoteColor);
	}
}

void AMainCharacter::ServerChangeEmotion_Implementation(int32 EmotionIndex, FLinearColor EmoteColor)
{
	MulticastChangeEmotion(EmotionIndex, EmoteColor);
}

void AMainCharacter::MulticastChangeEmotion_Implementation(int32 EmotionIndex, FLinearColor EmoteColor)
{
	SetEmotion(EmotionIndex, EmoteColor);
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

bool AMainCharacter::CanAct() const
{
	if (bIsStunned || bIsGroggy || bIsDead || bIsReviving)
	{
		return false;
	}
	return Super::CanAct();
}

bool AMainCharacter::CanMove() const
{
	if (bIsStunned || bIsGroggy || bIsDead || bIsReviving)
	{
		return false;
	}
	return Super::CanMove();
}

void AMainCharacter::Knockdown()
{
	if (!HasAuthority())
	{
		return;
	}

	// 이미 기절 상태면 무시
	if (bIsStunned)
	{
		return;
	}

	bIsStunned = true;

	// 물건을 들고 있었다면 떨어뜨림 (GrabOrDrop은 서버에서만 호출해야 함)
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	// 달리기 취소
	if (bIsSprinting)
	{
		ServerSetSprinting_Implementation(false);
	}

	// 애니메이션 재생 멀티캐스트 호출
	MulticastKnockdown();
}

void AMainCharacter::MulticastKnockdown_Implementation()
{
	if (KnockdownMontage && GetMesh() && GetMesh()->GetAnimInstance())
	{
		float AnimDuration = GetMesh()->GetAnimInstance()->Montage_Play(KnockdownMontage);
		
		// 몽타주가 재생되지 않았을 경우(0.0) 대비 안전장치
		if (AnimDuration <= 0.0f)
		{
			AnimDuration = 2.0f;
		}

		// 서버에서 타이머를 설정하여 기절 상태 복구
		if (HasAuthority())
		{
			GetWorldTimerManager().SetTimer(KnockdownTimerHandle, this, &AMainCharacter::OnKnockdownEnd, AnimDuration, false);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("KnockdownMontage가 할당되지 않았거나 재생할 수 없습니다. 즉시 회복됩니다."));
		if (HasAuthority())
		{
			OnKnockdownEnd();
		}
	}
}

void AMainCharacter::OnKnockdownEnd()
{
	if (HasAuthority())
	{
		bIsStunned = false;
	}
}

// ---------------------------------------------------------
// [그로기 및 사망 / 부활 시스템]
// ---------------------------------------------------------

void AMainCharacter::HandleHealthZero()
{
	if (bIsDead)
	{
		return;
	}

	if (DownCount == 0 && !bIsGroggy)
	{
		// 첫 번째 다운 (그로기)
		DownCount = 1;
		bIsGroggy = true;

		// 들고 있던 물건 떨어뜨리기
		if (CarryingComponent && CarryingComponent->IsCarrying())
		{
			CarryingComponent->GrabOrDrop();
		}
		
		// 달리기 취소
		bIsSprinting = false;
		GetCharacterMovement()->MaxWalkSpeed = NormalWalkSpeed;
		if (!HasAuthority())
		{
			ServerSetSprinting(false);
		}

		// 모든 클라이언트에 블루프린트 이벤트 방송 (표정 변경)
		MulticastOnEnterGroggy();
	}
	else if (DownCount >= 1)
	{
		// 두 번째 다운 (최종 사망)
		DownCount = 2;
		bIsDead = true;

		// 사망 시 인벤토리 슬롯의 모든 아이템을 사방으로 흩뿌림 (InventoryComponent)
		// 완전히 죽었을 때(bIsDead == true)만 이 블록이 실행되므로 안전합니다.
		if (InventoryComponent)
		{
			for (int32 i = 0; i < InventoryComponent->InventorySlots.Num(); ++i)
			{
				if (AItemBase* Item = InventoryComponent->InventorySlots[i])
				{
					// 수납 해제 후 렌더링 재활성화
					Item->MulticastOnEquipped(nullptr);
					
					// 랜덤한 포물선 방향(X, Y)과 위쪽으로 튕기는 힘(Z)을 계산합니다.
					FVector ScatterImpulse = FVector(
						FMath::RandRange(-400.f, 400.f), 
						FMath::RandRange(-400.f, 400.f), 
						FMath::RandRange(400.f, 700.f)
					);
					
					// Throw 함수를 사용해 아이템에 물리적인 힘을 가해 흩뿌립니다.
					Item->Throw(ScatterImpulse, this);

					InventoryComponent->InventorySlots[i] = nullptr;
					InventoryComponent->OnInventorySlotChanged.Broadcast(i, nullptr);
				}
			}
		}
		
		// 모든 클라이언트에 블루프린트 이벤트 방송
		MulticastOnDeath();
	}
}

void AMainCharacter::ReviveCharacter(float RestoredHealth)
{
	if (!bIsGroggy || bIsDead)
	{
		return;
	}

	bIsGroggy = false;
	bIsReviving = true;

	if (BaseAttribute)
	{
		BaseAttribute->SetHealth(RestoredHealth);
	}

	// 모든 클라이언트에 블루프린트 이벤트 방송 (표정 데 애니메이션 복구)
	MulticastOnRevived();

	// 애니메이션이 끝날 즈음(약 2.5초 후) 자동으로 FinishRevive를 호출하여 입력 차단 해제
	FTimerHandle ReviveTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(ReviveTimerHandle, this, &AMainCharacter::OnReviveTimerExpired, 2.5f, false);
}

void AMainCharacter::OnReviveTimerExpired()
{
	// 서버에서 실행된 타이머가 여기를 호출하면, Multicast RPC를 실행하여 모든 클라이언트의 bIsReviving을 강제로 내립니다.
	FinishRevive();
}

void AMainCharacter::FinishRevive_Implementation()
{
	// 서버와 모든 클라이언트에서 입력 차단을 해제
	bIsReviving = false;
}

// Multicast RPC 구현
void AMainCharacter::MulticastOnEnterGroggy_Implementation()
{
	OnEnterGroggy();
}

void AMainCharacter::MulticastOnRevived_Implementation()
{
	OnRevived();
}

void AMainCharacter::MulticastOnDeath_Implementation()
{
	OnDeath();
}

// 클라이언트가 F키를 눌러 서버에서 비로소 살려달라고 요청
void AMainCharacter::ServerReviveTarget_Implementation(AMainCharacter* Target)
{
	if (!Target || !Target->bIsGroggy || Target->bIsDead)
	{
		return;
	}

	if (BaseAttribute)
	{
		float MyHealth = BaseAttribute->GetHealth();
		float HealthToGive = MyHealth / 2.0f;
		if (HealthToGive < 1.0f) HealthToGive = 1.0f;
		float NewMyHealth = FMath::Max(1.0f, MyHealth - HealthToGive);

		BaseAttribute->SetHealth(NewMyHealth);
		Target->ReviveCharacter(HealthToGive);
	}
}

bool AMainCharacter::CanInteract_Implementation(AActor* Interactor) const
{
	// 자신이 그로기 상태이고 사망하지 않았으며, 상호작용을 시도하는 대상이 자신이 아닐 때 부활 상호작용 가능
	return (bIsGroggy && !bIsDead && Interactor != this);
}

void AMainCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bIsGroggy && !bIsDead)
	{
		AMainCharacter* Reviver = Cast<AMainCharacter>(Interactor);
		if (Reviver && Reviver->BaseAttribute)
		{
			// 살려주는 사람의 현재 체력 가져오기
			float ReviverCurrentHealth = Reviver->BaseAttribute->GetHealth();
			
			// 자신의 체력을 절반 깎고, 그 양을 쓰러진 사람에게 부여
			float HealthToGive = ReviverCurrentHealth / 2.0f;
			
			// 최소 체력 1은 남겨두기 위한 안전장치 (원한다면 제거 가능)
			if (HealthToGive < 1.0f) HealthToGive = 1.0f;
			float NewReviverHealth = FMath::Max(1.0f, ReviverCurrentHealth - HealthToGive);

			Reviver->BaseAttribute->SetHealth(NewReviverHealth);
			
			// 쓰러진 캐릭터 부활 처리
			ReviveCharacter(HealthToGive);
		}
	}
}

FText AMainCharacter::GetInteractPrompt_Implementation() const
{
	if (bIsGroggy && !bIsDead)
	{
		return FText::FromString(TEXT("살리기"));
	}
	return FText::GetEmpty();
}

void AMainCharacter::StartPushMode(AActor* TargetObject)
{
	if (!TargetObject) return;

	if (!HasAuthority())
	{
		ServerStartPushMode(TargetObject);
	}

	// 표정 변경 (서버에서 실행되면 내부적으로 Multicast를 통해 동기화됨)
	if (HasAuthority())
	{
		ChangeEmotion(EmotionIndex_Pushing);
	}

	// 무기/아이템 들고 있으면 해제
	if (CarryingComponent && CarryingComponent->IsCarrying())
	{
		CarryingComponent->GrabOrDrop();
	}

	bIsPushingMode = true;
	CurrentPushedObject = TargetObject;

	if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
	{
		EventObj->AddPusher(this);
	}

	// 캐릭터 정렬 연출: 거리는 그대로 유지하되, 대상(박스)을 바라보도록 회전만 수행
	FVector BoxCenter = TargetObject->GetComponentsBoundingBox().GetCenter();
	FVector Dir = BoxCenter - GetActorLocation();
	Dir.Z = 0.0f;
	Dir.Normalize();
	SetActorRotation(Dir.Rotation());
	LockedPushDirection = Dir; // 밀기 방향 고정

	// 이동 시 캐릭터가 회전하는 것을 방지 (항상 박스를 바라보도록 유지)
	if (GetCharacterMovement())
	{
		bOriginalOrientRotation = GetCharacterMovement()->bOrientRotationToMovement;
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}

	// 현재 캐릭터의 위치를 기준점으로 설정 (이동량 계산용)
	LastCharacterLocation = GetActorLocation();

	// 2. 물리 끄기 및 충돌 완벽 무시 (캐릭터가 상자를 밀 때 서로 겹쳐서 이동이 막히는 것을 방지)
	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetObject->GetRootComponent()))
	{
		// 물리 충돌로 인해 캐릭터가 밀려나는(미끄러지는) 현상을 원천 차단하기 위해 충돌 반응 자체를 Ignore로 변경
		PrimComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PrimComp->IgnoreActorWhenMoving(this, true);
		GetCapsuleComponent()->IgnoreActorWhenMoving(TargetObject, true);
		
		// 혹시 모를 캐릭터 메쉬와의 충돌도 예외 처리
		GetMesh()->IgnoreActorWhenMoving(TargetObject, true);
	}

	// 3. 상태 태그 부여 (애니메이션 연동)
	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Pushing"), false);
		StatusComponent->AddStatusTag(PushingTag);
	}

	// 4. 밀기 모드 이동 속도 저하
	if (GetCharacterMovement())
	{
		OriginalWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
		GetCharacterMovement()->MaxWalkSpeed = PushSpeed;
	}

	UE_LOG(LogTemp, Log, TEXT("밀기 모드 진입"));
}

void AMainCharacter::ServerStartPushMode_Implementation(AActor* TargetObject)
{
	StartPushMode(TargetObject);
}

void AMainCharacter::StopPushMode()
{
	if (!HasAuthority())
	{
		ServerStopPushMode();
	}

	// 표정 복구
	if (HasAuthority())
	{
		ChangeEmotion(EmotionIndex_Normal);
	}

	bIsPushingMode = false;
	
	// 물리 및 충돌 무시 원상 복구
	if (CurrentPushedObject)
	{
		if (AEventObjectBase* EventObj = Cast<AEventObjectBase>(CurrentPushedObject))
		{
			EventObj->RemovePusher(this);
		}

		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(CurrentPushedObject->GetRootComponent()))
		{
			PrimComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			PrimComp->IgnoreActorWhenMoving(this, false);
			GetCapsuleComponent()->IgnoreActorWhenMoving(CurrentPushedObject, false);
			GetMesh()->IgnoreActorWhenMoving(CurrentPushedObject, false);
		}
	}

	CurrentPushedObject = nullptr;

	// 2. 상태 태그 제거
	if (StatusComponent)
	{
		static const FGameplayTag PushingTag = FGameplayTag::RequestGameplayTag(FName("State.Pushing"), false);
		StatusComponent->RemoveStatusTag(PushingTag);
	}

	// 3. 이동 속도 및 회전 옵션 원상 복구
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = OriginalWalkSpeed;
		GetCharacterMovement()->bOrientRotationToMovement = bOriginalOrientRotation;
	}

	UE_LOG(LogTemp, Log, TEXT("밀기 모드 해제"));
}

void AMainCharacter::ServerStopPushMode_Implementation()
{
	StopPushMode();
}

void AMainCharacter::OnRep_IsPushingMode()
{
	// 리플리케이션(복제)에 따른 UI나 기타 상태 업데이트가 필요하면 이곳에 작성합니다.
	// bIsPushingMode 값 자체는 이미 서버와 동기화되었으므로 애니메이션은 정상 작동합니다.
}

void AMainCharacter::SetReviveUIVisibility(bool bIsVisible)
{
	if (ReviveWidgetComponent)
	{
		ReviveWidgetComponent->SetVisibility(bIsVisible);
	}
}
