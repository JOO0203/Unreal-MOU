#include "Base/EventObjectBase.h"
#include "Player/MainCharacter.h"
#include "Base/BaseAttributeSet.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

AEventObjectBase::AEventObjectBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// 보통 이벤트 오브젝트는 조금 무거운 느낌으로 설정
	ItemWeight = 50.0f;
	bCanBeStoredInInventory = false;
}

void AEventObjectBase::BeginPlay()
{
	Super::BeginPlay();

	// 밀기 전용 오브젝트의 경우 질량을 매우 높게 설정하여 캐릭터가 부딪혀서 밀리는 것을 방지
	if (bIsPushable)
	{
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
		{
			// 물리 충돌로 밀리는 현상 방지: 아예 물리 시뮬레이션을 끄고 스크립트로만 이동하게 만듦
			PrimComp->SetSimulatePhysics(false);

			// 레벨에 살짝 떠있을 경우를 대비해 시작 시 바닥으로 강제 스냅
			FVector BoxCenter, BoxExtents;
			GetActorBounds(false, BoxCenter, BoxExtents);
			FHitResult GroundHit;
			FVector Start = BoxCenter;
			FVector End = Start - FVector(0, 0, 1000.0f);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(this);
			if (GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_Visibility, Params))
			{
				// (원하는 바닥 위치 + 상자 반 높이) - 현재 상자 중심 = 이동해야 할 Z 거리
				float TargetCenterZ = GroundHit.ImpactPoint.Z + BoxExtents.Z;
				float ZOffset = TargetCenterZ - BoxCenter.Z;
				AddActorWorldOffset(FVector(0, 0, ZOffset));
			}
		}
	}
}

void AEventObjectBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEventObjectBase, CurrentPushers);
}

void AEventObjectBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 바닥에 떨어졌는지 체크 (추락 후 착지 감지)
	if (bIsFallingFromLedge)
	{
		if (FallTimer > 0.0f)
		{
			FallTimer -= DeltaTime;
		}
		else
		{
			// 속도가 0에 가까워지면 착지한 것으로 간주
			if (GetVelocity().SizeSquared() < 10.0f)
			{
				bIsFallingFromLedge = false;
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
				{
					// 몸으로 밀리지 않도록 다시 물리 시뮬레이션 끄기
					PrimComp->SetSimulatePhysics(false);
					
					// 바닥에 정확히 스냅
					FVector BoxCenter, BoxExtents;
					GetActorBounds(false, BoxCenter, BoxExtents);
					FHitResult GroundHit;
					FVector Start = BoxCenter;
					FVector End = Start - FVector(0, 0, 1000.0f);
					FCollisionQueryParams Params;
					Params.AddIgnoredActor(this);
					if (GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_Visibility, Params))
					{
						float TargetCenterZ = GroundHit.ImpactPoint.Z + BoxExtents.Z;
						float ZOffset = TargetCenterZ - BoxCenter.Z;
						AddActorWorldOffset(FVector(0, 0, ZOffset));
					}
				}
			}
		}
	}

	// [서버 중앙 제어: 밀기 이동 및 이탈/장애물 검사]
	if (HasAuthority() && bIsPushable && CurrentPushers.Num() > 0)
	{
		// 1. 손 앵커 포인트 기준 거리 이탈 검사 (상자가 이동하면서 원래 손 위치에서 80cm 이상 멀어지면 자동 해제)
		TArray<AMainCharacter*> PushersToDetach;
		for (AMainCharacter* Pusher : CurrentPushers)
		{
			if (!Pusher) continue;

			FVector LocalAnchor = PusherLocalAnchorMap.Contains(Pusher)
				? PusherLocalAnchorMap[Pusher]
				: GetActorTransform().InverseTransformPosition(Pusher->GetActorLocation());
			FVector WorldAnchor = GetActorTransform().TransformPosition(LocalAnchor);
			float Dist2D = FVector::Dist2D(Pusher->GetActorLocation(), WorldAnchor);

			if (Dist2D > PushDetachDistance)
			{
				PushersToDetach.Add(Pusher);
				UE_LOG(LogTemp, Warning, TEXT("[%s] 푸셔(%s)가 손 앵커에서 %fcm 멀어져(허용 %fcm) 밀기 모드가 자동 해제됩니다!"),
					*GetName(), *Pusher->GetName(), Dist2D, PushDetachDistance);
			}
		}

		for (AMainCharacter* Pusher : PushersToDetach)
		{
			Pusher->StopPushMode();
		}

		// 2. 상자 이동 조건 검사 및 이동 실행
		if (IsReadyToMove() && !bIsFallingFromLedge)
		{
			FVector TotalPushDir = FVector::ZeroVector;
			float TotalPushSpeed = 0.0f;
			float PushInputSign = 0.0f;
			int32 ActiveCount = 0;

			for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
			{
				if (Pusher && FMath::Abs(Pusher->GetCurrentPushInput()) > 0.1f)
				{
					float Input = Pusher->GetCurrentPushInput();
					PushInputSign = FMath::Sign(Input);
					TotalPushDir += Pusher->LockedPushDirection * PushInputSign;

					float Speed = Pusher->GetCharacterMovement() ? Pusher->GetCharacterMovement()->MaxWalkSpeed : 300.0f;
					TotalPushSpeed += Speed;
					ActiveCount++;
				}
			}

			if (ActiveCount > 0)
			{
				FVector PushDir = TotalPushDir / ActiveCount;
				PushDir.Z = 0.0f;
				PushDir.Normalize();

				float AvgSpeed = TotalPushSpeed / ActiveCount;
				FVector DeltaMove = PushDir * (AvgSpeed * DeltaTime);

				if (!DeltaMove.IsNearlyZero())
				{
					// 0. 전방 장애물(다른 상자나 벽) Sweep 감지
					FVector BoxCenter, BoxExtents;
					GetActorBounds(false, BoxCenter, BoxExtents);

					FVector TraceStart = BoxCenter + FVector(0, 0, 10.0f);
					FVector TraceEnd = TraceStart + DeltaMove;

					FCollisionQueryParams BoxParams;
					BoxParams.AddIgnoredActor(this);
					for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
					{
						if (Pusher) BoxParams.AddIgnoredActor(Pusher);
					}

					FHitResult BoxHit;
					bool bHitObstacle = GetWorld()->SweepSingleByChannel(
						BoxHit, TraceStart, TraceEnd,
						GetActorQuat(), ECC_Visibility,
						FCollisionShape::MakeBox(BoxExtents * 0.95f), BoxParams);

					// 수직 장애물에 부딪히지 않았을 때만 이동
					if (!bHitObstacle || FMath::Abs(BoxHit.ImpactNormal.Z) >= 0.5f)
					{
						AddActorWorldOffset(DeltaMove, false);

						// 1. 바닥 감지 (낭떠러지 체크)
						FVector TraceStartFall = BoxCenter;
						FVector TraceEndFall = TraceStartFall - FVector(0, 0, BoxExtents.Z + 50.0f);
						FHitResult FallHit;
						FCollisionQueryParams FallParams;
						FallParams.AddIgnoredActor(this);
						for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
						{
							if (Pusher) FallParams.AddIgnoredActor(Pusher);
						}

						bool bHitFall = GetWorld()->LineTraceSingleByChannel(FallHit, TraceStartFall, TraceEndFall, ECC_Visibility, FallParams);
						if (!bHitFall)
						{
							MulticastFallOffLedge();
						}
					}
				}
			}
		}
	}

	// 디버그 라인 그리기 (바운딩 박스 정중앙 기준)
	if (bShowDebugPushDistance)
	{
		FVector BoxCenter = GetComponentsBoundingBox().GetCenter();
		DrawDebugCircle(GetWorld(), BoxCenter, MaxPushDistance, 32, FColor::Green, false, -1.0f, 0, 2.0f, FVector(0, 1, 0), FVector(1, 0, 0), false);
	}
}

float AEventObjectBase::GetPushResistance_Implementation() const
{
	return ItemWeight;
}

void AEventObjectBase::Push_Implementation(AActor* Pusher, FVector PushDirection)
{
}

bool AEventObjectBase::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bIsPushable || !Interactor)
	{
		return false;
	}

	FBox Box = GetComponentsBoundingBox();
	FVector ClosestPoint = Box.GetClosestPointTo(Interactor->GetActorLocation());
	float Dist2D = FVector::Dist2D(Interactor->GetActorLocation(), ClosestPoint);
	return Dist2D <= MaxPushDistance;
}

void AEventObjectBase::Interact_Implementation(AActor* Interactor)
{
	if (!Interactor) return;

	if (!bIsPushable)
	{
		return;
	}

	FBox Box = GetComponentsBoundingBox();
	FVector ClosestPoint = Box.GetClosestPointTo(Interactor->GetActorLocation());
	float Dist2D = FVector::Dist2D(Interactor->GetActorLocation(), ClosestPoint);
	if (Dist2D > MaxPushDistance)
	{
		return;
	}

	if (AMainCharacter* MainChar = Cast<AMainCharacter>(Interactor))
	{
		MainChar->StartPushMode(this);
	}
}

void AEventObjectBase::UpdatePushersWeight()
{
	if (ItemWeight <= 0.0f || !HasAuthority())
	{
		return;
	}

	float WeightPerPusher = CurrentPushers.Num() > 0 ? (ItemWeight / CurrentPushers.Num()) : 0.0f;

	for (AMainCharacter* Pusher : CurrentPushers)
	{
		if (Pusher && Pusher->BaseAttribute)
		{
			UBaseAttributeSet* AttrSet = Pusher->BaseAttribute;

			// 기존에 분배된 무게가 있다면 일단 차감 (재분배를 위함)
			if (AppliedWeightMap.Contains(Pusher))
			{
				AttrSet->SetCurrentWeight(FMath::Max(0.0f, AttrSet->GetCurrentWeight() - AppliedWeightMap[Pusher]));
			}

			// 새로운 N분의 1 무게 부여
			AttrSet->SetCurrentWeight(AttrSet->GetCurrentWeight() + WeightPerPusher);
			AppliedWeightMap.Add(Pusher, WeightPerPusher);

			// 캐릭터 이동속도 및 과적 상태 즉시 갱신
			Pusher->UpdateCharacterSpeed();
		}
	}
}

void AEventObjectBase::AddPusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && !CurrentPushers.Contains(Pusher))
	{
		CurrentPushers.Add(Pusher);
		PusherLocalAnchorMap.Add(Pusher, GetActorTransform().InverseTransformPosition(Pusher->GetActorLocation()));
		UpdatePushersWeight();
	}
}

void AEventObjectBase::RemovePusher(AMainCharacter* Pusher)
{
	if (HasAuthority() && Pusher && CurrentPushers.Contains(Pusher))
	{
		if (AppliedWeightMap.Contains(Pusher))
		{
			if (Pusher->BaseAttribute)
			{
				Pusher->BaseAttribute->SetCurrentWeight(FMath::Max(0.0f, Pusher->BaseAttribute->GetCurrentWeight() - AppliedWeightMap[Pusher]));
				Pusher->UpdateCharacterSpeed();
			}
			AppliedWeightMap.Remove(Pusher);
		}

		PusherLocalAnchorMap.Remove(Pusher);
		CurrentPushers.Remove(Pusher);
		UpdatePushersWeight();
	}
}

bool AEventObjectBase::IsReadyToMove() const
{
	if (CurrentPushers.Num() < RequiredPushers)
	{
		return false;
	}

	// [대향/마주보고 밀기 충돌 검사] 두 명 이상의 활성 푸셔가 서로 마주보고 밀 경우 힘겨루기 상태로 이동 불가 (정지)
	if (CurrentPushers.Num() >= 2)
	{
		for (int32 i = 0; i < CurrentPushers.Num(); ++i)
		{
			AMainCharacter* PusherA = CurrentPushers[i];
			if (!PusherA || FMath::Abs(PusherA->GetCurrentPushInput()) < 0.1f) continue;

			FVector DirA = PusherA->LockedPushDirection * FMath::Sign(PusherA->GetCurrentPushInput());
			DirA.Z = 0.0f;
			DirA.Normalize();

			for (int32 j = i + 1; j < CurrentPushers.Num(); ++j)
			{
				AMainCharacter* PusherB = CurrentPushers[j];
				if (!PusherB || FMath::Abs(PusherB->GetCurrentPushInput()) < 0.1f) continue;

				FVector DirB = PusherB->LockedPushDirection * FMath::Sign(PusherB->GetCurrentPushInput());
				DirB.Z = 0.0f;
				DirB.Normalize();

				// 두 힘의 방향이 120도 이상 마주보거나 상반되면(Dot < -0.5f) 힘겨루기로 정지
				if (FVector::DotProduct(DirA, DirB) < -0.5f)
				{
					return false;
				}
			}
		}
	}

	// 1인 상자: 적어도 1명 이상의 푸셔가 W(+1.0) 또는 S(-1.0) 입력을 주고 있으면 이동 가능
	if (RequiredPushers <= 1)
	{
		for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
		{
			if (Pusher && FMath::Abs(Pusher->GetCurrentPushInput()) > 0.1f)
			{
				return true;
			}
		}
		return false;
	}

	// 2인 이상 필수 협동 상자: 최소 RequiredPushers 수만큼의 인원이 모두 동일한 방향으로 입력을 줘야 이동 가능
	float CommonSign = 0.0f;
	int32 ActivePusherCount = 0;

	for (const TObjectPtr<AMainCharacter>& Pusher : CurrentPushers)
	{
		if (Pusher)
		{
			float Input = Pusher->GetCurrentPushInput();
			if (FMath::Abs(Input) > 0.1f)
			{
				float Sign = FMath::Sign(Input);
				if (CommonSign == 0.0f)
				{
					CommonSign = Sign;
				}
				else if (CommonSign != Sign)
				{
					// 서로 반대 방향으로 밀고 당김 (충돌 -> 이동 불가)
					return false;
				}
				ActivePusherCount++;
			}
		}
	}

	return ActivePusherCount >= RequiredPushers;
}

void AEventObjectBase::MulticastFallOffLedge_Implementation()
{
	FallOffLedge();
}

void AEventObjectBase::FallOffLedge()
{
	// 상자를 밀던 모든 플레이어의 무게 회수 및 밀기 모드 강제 해제
	TArray<TObjectPtr<AMainCharacter>> PushersCopy = CurrentPushers;
	for (AMainCharacter* Pusher : PushersCopy)
	{
		if (Pusher)
		{
			if (AppliedWeightMap.Contains(Pusher) && Pusher->BaseAttribute)
			{
				Pusher->BaseAttribute->SetCurrentWeight(FMath::Max(0.0f, Pusher->BaseAttribute->GetCurrentWeight() - AppliedWeightMap[Pusher]));
				Pusher->UpdateCharacterSpeed();
			}
			Pusher->StopPushMode();
		}
	}
	CurrentPushers.Empty();
	AppliedWeightMap.Empty();
	PusherLocalAnchorMap.Empty();

	// 물리 시뮬레이션을 다시 켜서 바닥으로 추락하도록 만듦
	if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		PrimComp->SetSimulatePhysics(true);
	}

	bIsFallingFromLedge = true;
	FallTimer = 0.5f;
}
