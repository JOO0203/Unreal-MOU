#include "Item/TaserGun.h"
#include "Base/CharacterBase.h"
#include "Components/StatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"

ATaserGun::ATaserGun()
{
	// 테이저건은 최대 3회 사용 (요청 사양)
	MaxUseCount = 3;
	CurrentUseCount = 3;

	// 총구 지점 컴포넌트 (VFX 시작 위치용). BP에서 메시 총구로 이동시킴
	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshComponent);
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

// [TASER-003] 카메라 시점(화면 중앙) 기준 Line Trace, 맞은 캐릭터에 기절 부여
void ATaserGun::PerformTrace()
{
	// 트레이스는 든 플레이어의 카메라 시점(화면 정중앙) 기준으로 발사한다.
	// 총구 방향이 아니라 조준 방향이므로 메시 회전과 무관하게 정확히 조준된다.
	FVector ViewLocation = GetActorLocation();
	FRotator ViewRotation = GetActorRotation();

	APawn* OwnerPawn = Cast<APawn>(LastOwner);
	if (OwnerPawn && OwnerPawn->GetController())
	{
		OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else if (LastOwner)
	{
		// 컨트롤러가 없으면 액터 시점으로 폴백
		LastOwner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + ViewRotation.Vector() * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	// 이 총을 든 플레이어는 자기 자신을 맞히지 않도록 무시
	if (LastOwner)
	{
		Params.AddIgnoredActor(LastOwner);
	}

	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Pawn, Params);

	if (bHit && Hit.GetActor())
	{
		ApplyStun(Hit.GetActor());
	}

	// VFX 시작점은 총구(MuzzlePoint), 끝점은 트레이스 도착지점(히트면 히트, 아니면 최대거리)
	const FVector FxStart = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : TraceStart;
	const FVector FxEnd = bHit ? Hit.ImpactPoint : TraceEnd;
	MulticastPlayFireEffect(FxStart, FxEnd, bHit);
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
