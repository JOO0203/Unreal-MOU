#include "Item/TaserGun.h"
#include "Base/CharacterBase.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"

ATaserGun::ATaserGun()
{
	// 테이저건은 최대 3회 사용 (요청 사양)
	MaxUseCount = 3;
	CurrentUseCount = 3;
}

// [TASER-001] 좌클릭: 사용 횟수 차감 후 트레이스 발사
void ATaserGun::OnUse_Implementation()
{
	// 사용 전 잔여 횟수 확인 (Super가 차감하므로 미리 검사)
	if (CurrentUseCount <= 0)
	{
		return;
	}

	// ItemBase의 기본 로직: CurrentUseCount-- 수행
	Super::OnUse_Implementation();

	// 발사는 서버 권한에서 처리 (트레이스/기절 부여의 신뢰성 확보)
	if (HasAuthority())
	{
		PerformTrace();
	}
	else
	{
		ServerFire();
	}
}

// [TASER-002] 서버에서 실제 트레이스 + 기절 부여
void ATaserGun::ServerFire_Implementation()
{
	PerformTrace();
}

// [TASER-003] 총구 위치/방향으로 Line Trace, 맞은 캐릭터에 기절 부여
void ATaserGun::PerformTrace()
{
	// 총구 소켓이 있으면 그 위치/방향, 없으면 액터 위치+전방 사용
	FVector Start;
	FVector Forward;
	if (MeshComponent && MeshComponent->DoesSocketExist(MuzzleSocketName))
	{
		const FTransform SocketTM = MeshComponent->GetSocketTransform(MuzzleSocketName);
		Start = SocketTM.GetLocation();
		Forward = SocketTM.GetRotation().GetForwardVector();
	}
	else
	{
		Start = GetActorLocation();
		Forward = GetActorForwardVector();
	}

	const FVector End = Start + Forward * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	// 이 총을 든 플레이어는 자기 자신을 맞히지 않도록 무시
	if (LastOwner)
	{
		Params.AddIgnoredActor(LastOwner);
	}

	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	if (bHit && Hit.GetActor())
	{
		ApplyStun(Hit.GetActor());
	}

	// 모든 클라이언트에서 전기 이펙트 재생 (히트면 히트지점까지, 아니면 End까지)
	const FVector FxEnd = bHit ? Hit.ImpactPoint : End;
	MulticastPlayFireEffect(Start, FxEnd, bHit);
}

// [TASER-004] 대상 캐릭터에 기절 태그 부여 + 타이머로 해제 예약
void ATaserGun::ApplyStun(AActor* HitActor)
{
	ACharacterBase* TargetCharacter = Cast<ACharacterBase>(HitActor);
	if (!TargetCharacter)
	{
		return;
	}

	UStatusComponent* StatusComp = TargetCharacter->GetStatusComponent();
	if (!StatusComp)
	{
		return;
	}

	static const FGameplayTag StunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
	if (!StunTag.IsValid())
	{
		return;
	}

	// 기절 부여 (StatusComponent가 GAS Loose Tag까지 처리 + 복제)
	StatusComp->AddStatusTag(StunTag);

	// StunDuration 후 기절 해제 예약 (대상 캐릭터 기준 타이머)
	FTimerHandle StunTimerHandle;
	TWeakObjectPtr<UStatusComponent> WeakStatus = StatusComp;
	FTimerDelegate ClearDelegate = FTimerDelegate::CreateLambda([WeakStatus]()
	{
		static const FGameplayTag ClearStunTag = FGameplayTag::RequestGameplayTag(FName("State.Stunned"), false);
		if (WeakStatus.IsValid() && ClearStunTag.IsValid())
		{
			WeakStatus->RemoveStatusTag(ClearStunTag);
		}
	});

	TargetCharacter->GetWorldTimerManager().SetTimer(StunTimerHandle, ClearDelegate, StunDuration, false);
}

// [TASER-005] 발사 이펙트 훅 - 모든 클라이언트에서 BP VFX 이벤트 재생
void ATaserGun::MulticastPlayFireEffect_Implementation(FVector Start, FVector End, bool bHit)
{
	OnFireEffect(Start, End, bHit);
}
