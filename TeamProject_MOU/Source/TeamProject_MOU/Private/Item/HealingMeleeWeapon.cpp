#include "Item/HealingMeleeWeapon.h"

#include "AbilitySystemComponent.h"
#include "Ability/GA_SpannerSwing.h"
#include "TimerManager.h"
#include "Animation/AnimMontage.h"
#include "Base/BaseAttributeSet.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameplayEffect.h"
#include "Player/MainCharacter.h"

// [HEAL-000] 초기 컴포넌트와 기본값 설정
AHealingMeleeWeapon::AHealingMeleeWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent->SetRelativeLocation(FVector::ZeroVector);
	TargetTeam = EWeaponTargetTeam::Player;
	HitMode = EWeaponHitMode::Melee;
	ItemName = FText::FromString(TEXT("Healing Melee Weapon"));
}

// [HEAL-001] 타이머에서 개발용 판정 범위 표시
void AHealingMeleeWeapon::DrawSwingRange()
{
	// #if ENABLE_DRAW_DEBUG
	// if (bShowSwingRange && !IsHidden())
	// {
	// 	if (const AMainCharacter* Wielder = Cast<AMainCharacter>(GetOwningPawn()))
	// 	{
	// 		if (GetAttachParentActor() == Wielder)
	// 		{
	// 			const FVector Start = Wielder->GetActorLocation();
	// 			const FVector Forward = Wielder->GetBaseAimRotation().Vector();
	// 			const float Range = FMath::Max(1.0f, SwingRange);
	// 			const float Radius = FMath::Max(1.0f, SwingRadius);
	// 			DrawDebugCapsule(GetWorld(), Start + Forward * Range * 0.5f,
	// 				Range * 0.5f + Radius, Radius, FQuat::FindBetweenNormals(FVector::UpVector, Forward),
	// 				FColor::Yellow, false, 0.11f, 0, 1.5f);
	// 			DrawDebugSphere(GetWorld(), Start, Range, 32, FColor::Blue, false, 0.11f);
	// 			DrawDebugDirectionalArrow(GetWorld(), Start, Start + Forward * Range, 15.0f,
	// 				FColor::Yellow, false, 0.11f);
	// 		}
	// 	}
	// }
	// #endif
}

// [HEAL-005] 효과 적용 가능한 대상인지 검사
bool AHealingMeleeWeapon::IsValidTarget(AActor* HitActor) const
{
	const AMainCharacter* Player = Cast<AMainCharacter>(HitActor);
	return IsValid(Player) && Player != GetOwningPawn() && !Player->bIsDead;
}

// [HEAL-012] 디버그 표시 타이머 초기화
void AHealingMeleeWeapon::BeginPlay()
{
	Super::BeginPlay();
	// if (bShowSwingRange)
	// 	GetWorldTimerManager().SetTimer(DebugRangeTimer, this, &AHealingMeleeWeapon::DrawSwingRange, 0.1f, true);
}

// [HEAL-013] 제거 시 실행 중인 어빌리티와 타이머 정리
void AHealingMeleeWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DebugRangeTimer);
	if (ActiveSwingAbility.IsValid())
	{
		UGA_SpannerSwing* Ability = ActiveSwingAbility.Get();
		Ability->CancelAbility(Ability->GetCurrentAbilitySpecHandle(), Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo(), true);
	}
	Super::EndPlay(EndPlayReason);
}

// [HEAL-014] 현재 소유자의 장착·행동 가능 상태 검사
bool AHealingMeleeWeapon::IsSwingOwnerValid() const
{
	const AMainCharacter* Wielder = Cast<AMainCharacter>(GetOwningPawn());
	return HasAuthority() && IsValid(Wielder) && !Wielder->bIsDead && !Wielder->bIsGroggy && Wielder->CanAct()
		&& GetOwner() == Wielder && GetAttachParentActor() == Wielder && !IsHidden();
}

// [HEAL-015] 새 공격의 내구도·사용 상태 검사
bool AHealingMeleeWeapon::CanStartSwing() const
{
	return IsSwingOwnerValid() && !IsInUse() && CurrentDurability > 0.0f;
}

// [HEAL-003] 서버 ASC에 스패너 어빌리티를 일회 부여하고 실행
void AHealingMeleeWeapon::Fire()
{
	if (!CanStartSwing()) return;
	AMainCharacter* Wielder = Cast<AMainCharacter>(GetOwningPawn());
	if (UAbilitySystemComponent* ASC = Wielder->GetAbilitySystemComponent())
	{
		FGameplayAbilitySpec Spec(UGA_SpannerSwing::StaticClass(), 1, INDEX_NONE, this);
		ASC->GiveAbilityAndActivateOnce(Spec);
	}
}

// [HEAL-016] 어빌리티 시작 시 내구도 차감과 사용 상태 설정
void AHealingMeleeWeapon::BeginAbilitySwing(UGA_SpannerSwing* Ability)
{
	ActiveSwingAbility = Ability;
	bIsInUse = true;
	SwingWielder = Cast<AMainCharacter>(GetOwningPawn());
	CurrentDurability = FMath::Max(0.0f, CurrentDurability - FMath::Max(0.0f, DurabilityCostPerSwing));
	MulticastSwing(SwingWielder.Get());
}

// [HEAL-017] 어빌리티 종료 시 사용 상태 복원
void AHealingMeleeWeapon::EndAbilitySwing(UGA_SpannerSwing* Ability)
{
	if (ActiveSwingAbility.Get() != Ability) return;
	ActiveSwingAbility.Reset();
	SwingWielder.Reset();
	FinishUse();
}

// [HEAL-011] 서버 전방 스윕과 거리·가림 검사 후 한 명에게 치유 시도
void AHealingMeleeWeapon::ResolveSwing()
{
	AMainCharacter* Wielder = SwingWielder.Get();
	if (!HasAuthority() || !IsValid(Wielder) || Wielder->bIsDead || Wielder->bIsGroggy ||
		GetOwningPawn() != Wielder || GetOwner() != Wielder || GetAttachParentActor() != Wielder || IsHidden())
	{
		return;
	}

	// 카메라 위치 대신 캐릭터 중심에서 시작해 3인칭 카메라의 벽 관통을 방지한다.
	const FVector Start = Wielder->GetActorLocation();
	const FVector Forward = Wielder->GetBaseAimRotation().Vector();
	const float Range = FMath::Max(1.0f, SwingRange);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HealingMeleeSwing), false, Wielder);
	Params.AddIgnoredActor(this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(Hits, Start, Start + Forward * Range,
		FQuat::Identity, Objects, FCollisionShape::MakeSphere(FMath::Max(1.0f, SwingRadius)), Params);
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });

	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!IsValidTarget(Target))
		{
			continue;
		}
		const FVector Offset = Target->GetActorLocation() - Start;
		if (FVector::DotProduct(Offset, Forward) <= 0.0f || Offset.SizeSquared() > FMath::Square(Range))
		{
			continue;
		}
		FHitResult Obstruction;
		if (GetWorld()->LineTraceSingleByChannel(Obstruction, Start, Target->GetActorLocation(), ECC_Visibility, Params)
			&& Obstruction.GetActor() != Target)
		{
			continue;
		}
		bResolvingSwing = true;
		ApplyWeaponHit(Target, Hit);
		bResolvingSwing = false;
		break; // 한 번 휘두를 때 한 명에게 한 번만 적용.
	}
}

// [HEAL-006] 명중 대상의 효과 처리
void AHealingMeleeWeapon::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	if (!HasAuthority() || !bResolvingSwing || !IsValidTarget(HitActor) || HealAmount <= 0.0f)
	{
		return;
	}
	UAbilitySystemComponent* ASC = Cast<AMainCharacter>(HitActor)->GetAbilitySystemComponent();
	if (!ASC || !ASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetHealthAttribute()) ||
		ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.0f ||
		ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) >= ASC->GetNumericAttribute(UBaseAttributeSet::GetMaxHealthAttribute()))
	{
		return;
	}

	UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UBaseAttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(HealAmount);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(GetOwningPawn(), this);
	Context.AddSourceObject(this);
	ASC->ApplyGameplayEffectToSelf(Effect, 1.0f, Context);
	MulticastHealEffect(HitActor, HitActor->GetActorLocation());
}

// [HEAL-007] 모든 클라이언트에서 추가 휘두르기 연출 호출 (몽타주는 GAS에서 재생)
void AHealingMeleeWeapon::MulticastSwing_Implementation(AMainCharacter* Wielder)
{
	OnSwingEffect();
}

// [HEAL-008] 모든 클라이언트에서 치유 연출 호출
void AHealingMeleeWeapon::MulticastHealEffect_Implementation(AActor* Target, FVector Location)
{
	OnHealEffect(Target, Location);
}
