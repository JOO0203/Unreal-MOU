#include "Item/SprayItem.h"
#include "Components/CarryingComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/World.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Player/MainCharacter.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h" // [DEBUG-SPRAY] 트레이스 시각화용 (디버그 끝나면 제거)

// [SPRAY-000] 기본 분사 주기와 용량 설정.
ASprayItem::ASprayItem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.03f;
	MaxUseCount = 60;
	NozzleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NozzleMesh"));
	NozzleMesh->SetupAttachment(MeshComponent);
	NozzleMesh->SetMobility(EComponentMobility::Movable);
	NozzleMesh->SetSimulatePhysics(false);
	NozzleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NozzleMesh->SetGenerateOverlapEvents(false);
	SprayAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("SprayAudioComponent"));
	SprayAudioComponent->SetupAttachment(NozzleMesh);
	SprayAudioComponent->bAutoActivate = false;
}

// [SPRAY-018] 노즐을 포함한 공통 바운딩박스 중심 보정을 사용하지 않는다.
bool ASprayItem::ShouldCenterOnCarrySocket() const
{
	return false;
}

// [SPRAY-019] 몸통의 잡는 지점을 스케일과 회전을 반영하여 손 소켓에 맞춘다.
FVector ASprayItem::GetCarryLocationOffset() const
{
	if (!MeshComponent || !MeshComponent->GetStaticMesh()) return HandLocationOffset;
	const UStaticMesh* BodyMesh = MeshComponent->GetStaticMesh();
	// 에디터의 확장 경계는 원본 MeshDescription 캐시를 참조할 수 있다.
	// Build Scale이 반영된 실제 렌더 메시의 중심을 우선 사용한다.
	const FStaticMeshRenderData* RenderData = BodyMesh->GetRenderData();
	const FVector BodyCenter = RenderData ? FVector(RenderData->Bounds.Origin) : FVector(BodyMesh->GetBounds().Origin);
	const FVector GripPoint = BodyCenter + GripPointOffset;
	// 호출 시 손 소켓에 이미 부착되어 있으므로 상대 스케일에 부모 스케일 보정도 포함된다.
	const FVector ScaledGripPoint = GripPoint * MeshComponent->GetRelativeScale3D();
	return HandLocationOffset - HandRotationOffset.RotateVector(ScaledGripPoint);
}

// [SPRAY-020] 손 소켓 기준 스프레이 방향을 반환한다.
FRotator ASprayItem::GetCarryRotationOffset() const
{
	return HandRotationOffset;
}

// [SPRAY-016] BP에서 배치한 노즐의 기본 위치를 저장한다.
void ASprayItem::BeginPlay()
{
	Super::BeginPlay();
	if (NozzleMesh) NozzleRestLocation = NozzleMesh->GetRelativeLocation();
	if (SprayAudioComponent && SprayLoopSound)
	{
		SprayAudioComponent->SetSound(SprayLoopSound);
	}
	UpdateSpraySound(bSpraying);
}

// [SPRAY-017] 분사 상태에 따라 노즐을 누르거나 기본 위치로 복귀시킨다.
void ASprayItem::UpdateNozzle(float DeltaSeconds)
{
	if (!NozzleMesh || GetNetMode() == NM_DedicatedServer) return;
	const float TargetAlpha = bSpraying ? 1.f : 0.f;
	NozzlePressAlpha = NozzleTravelSeconds > KINDA_SMALL_NUMBER
		? FMath::FInterpConstantTo(NozzlePressAlpha, TargetAlpha, DeltaSeconds, 1.f / NozzleTravelSeconds)
		: TargetAlpha;
	NozzleMesh->SetRelativeLocation(NozzleRestLocation + NozzlePressedOffset * NozzlePressAlpha);
}

// [SPRAY-001] 서버에서 표면 검사 및 지속 소비.
void ASprayItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 분사가 끝난 뒤와 클라이언트에서도 노즐 복귀를 계속 처리한다.
	UpdateNozzle(DeltaSeconds);
	if (!HasAuthority() || !bSpraying) return;
	AMainCharacter* Holder = Cast<AMainCharacter>(GetOwner());
	UCarryingComponent* Carry = Holder ? Holder->FindComponentByClass<UCarryingComponent>() : nullptr;
	if (!Holder || !Holder->CanAct() || !Carry || Carry->GetCarriedActor() != this || ConsumeUseCount <= 0)
	{
		SetSpraying(false);
		return;
	}
	SpraySurface();
	ConsumeElapsed += DeltaSeconds;
	const float Interval = FMath::Max(SecondsPerUse, 0.1f);
	while (ConsumeElapsed >= Interval && ConsumeUseCount > 0)
	{
		ConsumeElapsed -= Interval;
		--ConsumeUseCount;
	}
	if (ConsumeUseCount <= 0)
	{
		SetSpraying(false);
		Carry->ClearCarriedItem();
		Destroy();
	}
}

// [SPRAY-002] 좌클릭으로 분사 시작 요청.
void ASprayItem::OnUse_Implementation()
{
	if (HasAuthority()) SetSpraying(true);
	else ServerSetSpraying(true);
}

// [SPRAY-003] 입력 해제 시 서버에 분사 중지를 요청한다.
void ASprayItem::OnUseReleased_Implementation()
{
	if (HasAuthority()) SetSpraying(false);
	else ServerSetSpraying(false);
}

// [SPRAY-021] 첫 습득 경로에서도 장착 경로와 동일하게 Idle 애니메이션을 시작한다.
void ASprayItem::PickUp_Implementation(AActor* Picker)
{
	Super::PickUp_Implementation(Picker);
	if (HasAuthority() && Picker)
	{
		MulticastPlaySprayAnimation(Picker, false);
	}
}

// [SPRAY-004] 장착 시 서버 RPC 소유권 복원.
void ASprayItem::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);
	if (HasAuthority()) SetOwner(Equipper);
	HandleSprayAnimationState(Equipper, false);
}

// [SPRAY-005] 수납 시 분사 중지.
void ASprayItem::OnUnequipped_Implementation(AActor* Equipper)
{
	if (HasAuthority()) SetSpraying(false);
	StopSprayAnimation(Equipper);
	Super::OnUnequipped_Implementation(Equipper);
}

// [SPRAY-006] 내려놓기 전에 분사 중지.
void ASprayItem::Drop_Implementation(FVector Location, AActor* Dropper)
{
	if (HasAuthority())
	{
		SetSpraying(false);
		MulticastStopSprayAnimation(Dropper ? Dropper : GetOwner());
	}
	Super::Drop_Implementation(Location, Dropper);
}

// [SPRAY-007] 던지기 전에 분사 중지.
void ASprayItem::Throw_Implementation(FVector Velocity, AActor* Thrower)
{
	if (HasAuthority())
	{
		SetSpraying(false);
		MulticastStopSprayAnimation(Thrower ? Thrower : GetOwner());
	}
	Super::Throw_Implementation(Velocity, Thrower);
}

// [SPRAY-008] 분사 상태 복제 등록.
void ASprayItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASprayItem, bSpraying);
}

// [SPRAY-010] 복제된 분사 상태를 이펙트에 전달.
void ASprayItem::OnRep_Spraying()
{
	OnSprayStateChanged(bSpraying);
	UpdateSpraySound(bSpraying);
}

// [SPRAY-028] 전용 서버를 제외한 각 클라이언트에서 분사 중에만 루프 사운드를 유지한다.
void ASprayItem::UpdateSpraySound(bool bActive)
{
	if (!SprayAudioComponent || GetNetMode() == NM_DedicatedServer) return;
	if (bActive)
	{
		if (SprayLoopSound && SprayAudioComponent->GetSound() != SprayLoopSound)
		{
			SprayAudioComponent->SetSound(SprayLoopSound);
		}
		if (SprayAudioComponent->GetSound() && !SprayAudioComponent->IsPlaying())
		{
			SprayAudioComponent->Play();
		}
	}
	else if (SprayAudioComponent->IsPlaying())
	{
		SprayAudioComponent->Stop();
	}
}

// [SPRAY-011] 서버에서 분사 시작/중지 요청 검증.
void ASprayItem::ServerSetSpraying_Implementation(bool bActive)
{
	SetSpraying(bActive);
}

// [SPRAY-012] 서버 분사 상태 전환.
void ASprayItem::SetSpraying(bool bActive)
{
	if (!HasAuthority()) return;
	AMainCharacter* Holder = Cast<AMainCharacter>(GetOwner());
	if (bActive)
	{
		UCarryingComponent* Carry = Holder ? Holder->FindComponentByClass<UCarryingComponent>() : nullptr;
		if (!Holder || !Holder->CanAct() || !Carry || Carry->GetCarriedActor() != this || ConsumeUseCount <= 0) return;
	}
	if (bSpraying == bActive) return;
	bSpraying = bActive;
	OnRep_Spraying();
	MulticastPlaySprayAnimation(Holder, bSpraying);
	ForceNetUpdate();
}

// [SPRAY-022] 장착 캐릭터의 Idle/Press 애니메이션을 모든 클라이언트에서 동일하게 전환한다.
void ASprayItem::MulticastPlaySprayAnimation_Implementation(AActor* Holder, bool bPressed)
{
	HandleSprayAnimationState(Holder, bPressed);
}

// [SPRAY-026] 광클릭 중에는 Press를 유지하고, 완전히 놓았을 때만 지연 후 Idle로 복귀한다.
void ASprayItem::HandleSprayAnimationState(AActor* Holder, bool bPressed)
{
	SprayAnimationHolder = Holder;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SprayIdleReturnTimer);
	}

	if (bPressed)
	{
		// 해제 지연 중 다시 눌렀다면 기존 Press를 그대로 유지해 시작 프레임 반복을 막는다.
		if (bHasActiveSprayAnimation && bShowingPressAnimation)
		{
			return;
		}
		PlaySprayAnimation(Holder, true);
		return;
	}

	// 처음 장착했거나 이미 Idle이라면 즉시 Idle을 보장하고 중복 재생은 하지 않는다.
	if (!bHasActiveSprayAnimation)
	{
		PlaySprayAnimation(Holder, false);
		return;
	}
	if (!bShowingPressAnimation)
	{
		return;
	}

	if (SprayAnimationReleaseDelay <= KINDA_SMALL_NUMBER)
	{
		PlayIdleAfterRelease();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SprayIdleReturnTimer,
			this,
			&ASprayItem::PlayIdleAfterRelease,
			SprayAnimationReleaseDelay,
			false);
	}
}

// [SPRAY-027] 짧은 입력 공백이 끝날 때 Press 몽타주를 한 번만 Idle로 전환한다.
void ASprayItem::PlayIdleAfterRelease()
{
	if (SprayAnimationHolder.IsValid())
	{
		PlaySprayAnimation(SprayAnimationHolder.Get(), false);
	}
}

// [SPRAY-023] 기존 시퀀스를 DefaultSlot 동적 몽타주로 만들어 장착 중 반복 재생한다.
void ASprayItem::PlaySprayAnimation(AActor* Holder, bool bPressed)
{
	ACharacter* Character = Cast<ACharacter>(Holder);
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	UAnimSequenceBase* Animation = bPressed ? SprayPressAnimation : SprayIdleAnimation;
	if (!AnimInstance || !Animation || SprayAnimationSlot.IsNone())
	{
		return;
	}

	if (ActiveSprayMontage.IsValid())
	{
		AnimInstance->Montage_Stop(SprayAnimationBlendTime, ActiveSprayMontage.Get());
	}

	ActiveSprayMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Animation,
		SprayAnimationSlot,
		SprayAnimationBlendTime,
		SprayAnimationBlendTime,
		1.0f,
		MAX_int32);
	if (ActiveSprayMontage.IsValid())
	{
		bHasActiveSprayAnimation = true;
		bShowingPressAnimation = bPressed;
	}
}

// [SPRAY-024] 스프레이 장착 해제·드롭·투척 시 모든 클라이언트의 스프레이 몽타주를 정지한다.
void ASprayItem::MulticastStopSprayAnimation_Implementation(AActor* Holder)
{
	StopSprayAnimation(Holder);
}

// [SPRAY-025] 다른 DefaultSlot 몽타주를 건드리지 않고 이 스프레이가 시작한 몽타주만 정지한다.
void ASprayItem::StopSprayAnimation(AActor* Holder)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SprayIdleReturnTimer);
	}
	ACharacter* Character = Cast<ACharacter>(Holder);
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (AnimInstance && ActiveSprayMontage.IsValid())
	{
		AnimInstance->Montage_Stop(SprayAnimationBlendTime, ActiveSprayMontage.Get());
	}
	ActiveSprayMontage.Reset();
	SprayAnimationHolder.Reset();
	bHasActiveSprayAnimation = false;
	bShowingPressAnimation = false;
}

// [SPRAY-013] 표면 검사 후 반지름 밖에서만 새 데칼 생성.
void ASprayItem::SpraySurface()
{
	APawn* Holder = Cast<APawn>(GetOwner());
	if (!Holder || !Holder->GetController() || !DecalMaterial)
	{
		// [DEBUG-SPRAY] 사전 조건 실패 (소유자/컨트롤러/DecalMaterial 중 하나 없음)
		// UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] 사전조건 실패: Holder=%d Controller=%d DecalMaterial=%d"),
		// 	Holder != nullptr ? 1 : 0, (Holder && Holder->GetController()) ? 1 : 0, DecalMaterial != nullptr ? 1 : 0);
		return;
	}
	FVector Start;
	FRotator Rotation;
	Holder->GetController()->GetPlayerViewPoint(Start, Rotation);
	const FVector End = Start + Rotation.Vector() * FMath::Max(SprayRange, 1.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpraySurface), true, this);
	Params.AddIgnoredActor(Holder);
	FHitResult Hit;

	//// [DEBUG-SPRAY] 트레이스 방향/시작·끝점 로그 + 화면에 선 그리기(빨강)
	//UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] Start=%s Dir=%s End=%s Range=%.0f"),
	//	*Start.ToString(), *Rotation.Vector().ToString(), *End.ToString(), SprayRange);
	//DrawDebugLine(GetWorld(), Start, End, FColor::Red, false, 2.0f, 0, 1.0f);

	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		// [DEBUG-SPRAY] 트레이스가 아무것도 안 맞음 (Visibility Block 표면이 없음)
		/*UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] 트레이스 히트 없음 (Visibility로 막는 게 없음)"));*/
		return;
	}
	UPrimitiveComponent* Surface = Hit.GetComponent();

	// [DEBUG-SPRAY] 맞은 대상 + Receives Decals 여부 + 히트 지점(노란 점)
	/*UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] 히트! Actor=%s Comp=%s bReceivesDecals=%d ImpactPoint=%s"),
		*GetNameSafe(Hit.GetActor()), *GetNameSafe(Surface),
		Surface ? (Surface->bReceivesDecals ? 1 : 0) : -1, *Hit.ImpactPoint.ToString());
	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 12.0f, FColor::Yellow, false, 2.0f);*/

	if (!Surface || !Surface->bReceivesDecals)
	{
		//// [DEBUG-SPRAY] 맞긴 했는데 그 표면이 Receives Decals 꺼짐 → 데칼 안 찍힘
		//UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] 표면의 Receives Decals가 꺼져 있음 → 데칼 스킵"));
		//return;
	}
	const FTransform Transform = Surface->GetSocketTransform(Hit.BoneName);
	const float Radius = FMath::Max(DecalRadius, 1.f);
	if (LastSurface == Surface && LastBone == Hit.BoneName
		&& FVector::DotProduct(Transform.TransformVectorNoScale(LastLocalNormal).GetSafeNormal(), Hit.ImpactNormal) > 0.95f
		&& FVector::DistSquared(Transform.TransformPosition(LastLocalPoint), Hit.ImpactPoint) <= FMath::Square(Radius)) return;
	LastSurface = Surface;
	LastBone = Hit.BoneName;
	LastLocalPoint = Transform.InverseTransformPosition(Hit.ImpactPoint);
	LastLocalNormal = Transform.InverseTransformVectorNoScale(Hit.ImpactNormal);
	MulticastStamp(Surface, LastLocalPoint, LastLocalNormal, Hit.BoneName);
}

// [SPRAY-014] 각 클라이언트에서 피격 컴포넌트에 데칼 부착.
void ASprayItem::MulticastStamp_Implementation(UPrimitiveComponent* Surface, FVector Location, FVector Normal, FName Bone)
{
	if (GetNetMode() == NM_DedicatedServer || !IsValid(Surface) || !DecalMaterial)
	{
		// [DEBUG-SPRAY] Multicast 진입 실패 (전용서버/표면무효/머티리얼없음)
		// UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] Stamp 리턴: NetMode=%d SurfaceValid=%d DecalMaterial=%d"),
		// 	(int32)GetNetMode(), IsValid(Surface) ? 1 : 0, DecalMaterial != nullptr ? 1 : 0);
		return;
	}
	Decals.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Decal) { return !Decal.IsValid(); });
	while (Decals.Num() >= FMath::Max(MaxDecals, 1))
	{
		if (Decals[0].IsValid()) Decals[0]->DestroyComponent();
		Decals.RemoveAt(0);
	}
	const FTransform Transform = Surface->GetSocketTransform(Bone);
	const FVector WorldNormal = Transform.TransformVectorNoScale(Normal).GetSafeNormal();
	const FVector DecalSize(FMath::Max(ProjectionDepth, 0.1f), FMath::Max(DecalRadius, 1.f), FMath::Max(DecalRadius, 1.f));
	const FVector DecalLoc = Transform.TransformPosition(Location);
	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAttached(DecalMaterial,
		DecalSize, Surface, Bone, DecalLoc, (-WorldNormal).Rotation(),
		EAttachLocation::KeepWorldPosition, FMath::Max(DecalLifeSeconds, 1.f));

	//// [DEBUG-SPRAY] 데칼 스폰 결과 + 크기/위치 로그 + 위치에 초록 점
	//UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] SpawnDecal 결과=%s Size=%s Loc=%s Life=%.0f"),
	//	Decal ? TEXT("성공") : TEXT("NULL(실패)"), *DecalSize.ToString(), *DecalLoc.ToString(), DecalLifeSeconds);
	//DrawDebugPoint(GetWorld(), DecalLoc, 20.0f, FColor::Green, false, 3.0f);

	// [DEBUG-SPRAY] 데칼 투영 방향 확인: 데칼은 +X축으로 투영됨.
	// 파란 선 = 데칼이 실제 쏘는 방향(+X). 이게 벽 안(뒤)으로 향하면 투영이 안 됨.
	if (Decal)
	{
		// const FVector DecalForward = Decal->GetComponentQuat().GetForwardVector();
		// UE_LOG(LogTemp, Warning, TEXT("[DEBUG-SPRAY] WorldNormal=%s DecalForward(+X)=%s (Forward가 벽 바깥=Normal과 같은쪽이어야 정상)"),
		// 	*WorldNormal.ToString(), *DecalForward.ToString());
		/*DrawDebugLine(GetWorld(), DecalLoc, DecalLoc + DecalForward * 60.f, FColor::Blue, false, 3.0f, 0, 2.0f);
		DrawDebugLine(GetWorld(), DecalLoc, DecalLoc + WorldNormal * 60.f, FColor::Cyan, false, 3.0f, 0, 2.0f);*/
	}

	if (Decal)
	{
		Decal->SetSortOrder(NextSortOrder++);
		Decals.Add(Decal);
	}
}

// [SPRAY-015] 소진 및 레벨 종료 시 분사 이펙트 정리.
void ASprayItem::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSprayAnimation(GetOwner() ? GetOwner() : GetAttachParentActor());
	UpdateSpraySound(false);
	OnSprayStateChanged(false);
	Super::EndPlay(Reason);
}
