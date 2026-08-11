#include "Item/WeaponItemBase.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

AWeaponItemBase::AWeaponItemBase()
{
	// 근접 타격 콜라이더 (평소 꺼둠, BP에서 크기/위치 조정)
	MeleeCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("MeleeCollider"));
	MeleeCollider->SetupAttachment(MeshComponent);
	MeleeCollider->SetBoxExtent(FVector(20.0f, 20.0f, 20.0f));
	// 평소엔 충돌 없음 (휘두를 때 EnableMeleeCollision(true)로 켬)
	MeleeCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeleeCollider->SetGenerateOverlapEvents(false);
}

void AWeaponItemBase::BeginPlay()
{
	Super::BeginPlay();

	// 근접 콜라이더 오버랩 콜백 바인딩
	if (MeleeCollider)
	{
		MeleeCollider->OnComponentBeginOverlap.AddDynamic(this, &AWeaponItemBase::OnMeleeOverlap);
	}
}

// [WEAPON-000] 좌클릭: 잔여 횟수 체크 → 차감 → 서버 권한에서 Fire()
void AWeaponItemBase::OnUse_Implementation()
{
	// 잔여 사용 횟수 확인 (Super가 차감하므로 미리 검사)
	if (CurrentUseCount <= 0)
	{
		return;
	}

	// ItemBase의 기본 로직: CurrentUseCount-- 수행
	Super::OnUse_Implementation();

	// 발사는 서버 권한에서 처리 (판정/효과의 신뢰성 확보)
	if (HasAuthority())
	{
		Fire();
	}
	else
	{
		ServerFire();
	}
}

// [WEAPON-007] 실제 발사 로직 (기본 빈 구현, 자식이 override)
void AWeaponItemBase::Fire()
{
	// 자식 클래스에서 구현 (테이저=트레이스, 칼=콜라이더 등)
}

// [WEAPON-008] 클라이언트 → 서버 발사 위임
void AWeaponItemBase::ServerFire_Implementation()
{
	Fire();
}

// [WEAPON-005] TargetTeam을 실제 트레이스 채널로 변환
// ECC_GameTraceChannel1 = "NPC"(적), ECC_GameTraceChannel2 = "Player" (DefaultEngine.ini 정의)
ECollisionChannel AWeaponItemBase::GetTargetTraceChannel() const
{
	switch (TargetTeam)
	{
	case EWeaponTargetTeam::Player:
		return ECC_GameTraceChannel2; // Player
	case EWeaponTargetTeam::Enemy:
	default:
		return ECC_GameTraceChannel1; // NPC
	}
}

// [WEAPON-001] 근접 콜라이더 오버랩 on/off
void AWeaponItemBase::EnableMeleeCollision(bool bEnable)
{
	if (!MeleeCollider)
	{
		return;
	}

	if (bEnable)
	{
		MeleeCollider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeleeCollider->SetGenerateOverlapEvents(true);
	}
	else
	{
		MeleeCollider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeleeCollider->SetGenerateOverlapEvents(false);
	}
}

// [WEAPON-002] 근접 콜라이더 오버랩 콜백
void AWeaponItemBase::OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 판정은 서버 권한에서만
	if (!HasAuthority())
	{
		return;
	}

	// 자기 자신 / 든 플레이어는 무시
	if (!OtherActor || OtherActor == this || OtherActor == LastOwner)
	{
		return;
	}

	// 진영 확인: 대상 콜리전이 TargetTeam 채널을 Block/Overlap 하는지로 피아식별
	const ECollisionChannel TargetChannel = GetTargetTraceChannel();
	if (OtherComp)
	{
		const ECollisionResponse Response = OtherComp->GetCollisionResponseToChannel(TargetChannel);
		if (Response == ECR_Ignore)
		{
			// 노리는 진영이 아님 (아군 등) → 무시
			return;
		}
	}

	ApplyWeaponHit(OtherActor, SweepResult);
}

// [WEAPON-003] 즉발 트레이스 발사 (피아식별: TargetTeam 채널만 트레이스)
bool AWeaponItemBase::FireHitscan(const FVector& Start, const FVector& End, FHitResult& OutHit)
{
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (LastOwner)
	{
		Params.AddIgnoredActor(LastOwner);
	}

	// TargetTeam 채널로만 트레이스 → 다른 진영은 아예 안 걸림 (피아식별)
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutHit, Start, End, GetTargetTraceChannel(), Params);

	if (bHit && OutHit.GetActor())
	{
		ApplyWeaponHit(OutHit.GetActor(), OutHit);
	}

	return bHit;
}

// [WEAPON-004] 공통 히트 처리 (기본은 빈 구현, 자식이 override)
void AWeaponItemBase::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	// 자식 클래스에서 구현 (테이저건=기절, 칼=데미지 등)
}

// ---------------------------------------------------------------------------
// [WEAPON] 원거리 발사체 (Projectile) - 현재 미사용, 주석 처리.
//   나중에 날아가는 투사체가 필요하면 헤더의 주석과 함께 아래 주석을 풀 것.
// ---------------------------------------------------------------------------
// AActor* AWeaponItemBase::FireProjectile(const FVector& SpawnLocation, const FVector& Direction)
// {
//	if (!ProjectileClass || !GetWorld())
//	{
//		return nullptr;
//	}
//
//	FActorSpawnParameters SpawnParams;
//	SpawnParams.Owner = this;
//	SpawnParams.Instigator = Cast<APawn>(LastOwner);
//	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
//
//	const FRotator SpawnRot = Direction.Rotation();
//	AActor* Projectile = GetWorld()->SpawnActor<AActor>(ProjectileClass, SpawnLocation, SpawnRot, SpawnParams);
//	// TODO: Projectile에 UProjectileMovementComponent를 붙이고 InitialSpeed=ProjectileSpeed 설정,
//	//       Projectile 쪽에서 TargetTeam 채널로 히트 판정 → ApplyWeaponHit 호출 연동.
//	return Projectile;
// }
