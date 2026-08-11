#pragma once

#include "CoreMinimal.h"
#include "Base/ItemBase.h"
#include "WeaponItemBase.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/**
 * 무기 대상 진영 (피아식별용)
 * 프로젝트에 이미 정의된 Trace Channel과 매핑:
 *   ECC_GameTraceChannel1 = "NPC"    (적)
 *   ECC_GameTraceChannel2 = "Player"
 */
UENUM(BlueprintType)
enum class EWeaponTargetTeam : uint8
{
	// 적(NPC)만 맞힘
	Enemy		UMETA(DisplayName = "Enemy (NPC)"),
	// 플레이어만 맞힘
	Player		UMETA(DisplayName = "Player")
};

/**
 * 무기 명중 방식
 */
UENUM(BlueprintType)
enum class EWeaponHitMode : uint8
{
	// 근접: 콜라이더 오버랩으로 명중
	Melee		UMETA(DisplayName = "Melee (Collider)"),
	// 원거리: 즉발 Line Trace로 명중 (발사체는 별도, 현재 주석)
	Ranged		UMETA(DisplayName = "Ranged (Trace)")
};

/**
 * AWeaponItemBase
 * 무기 아이템 공통 베이스. 주 목적 두 가지:
 *   1) 피아식별 - 커스텀 Trace Channel(NPC/Player)로 대상 진영 구분
 *   2) 콜라이더 - 근접용 오버랩 콜라이더 제공 (원거리 발사체는 주석 처리)
 * 자식(테이저건, 칼 등)은 ApplyWeaponHit만 override해서 실제 효과를 넣는다.
 */
UCLASS()
class TEAMPROJECT_MOU_API AWeaponItemBase : public AItemBase
{
	GENERATED_BODY()

public:
	AWeaponItemBase();

protected:
	virtual void BeginPlay() override;

#pragma region [WEAPON] 사용/발사 흐름 (공통)
	// [WEAPON-000] 좌클릭: 잔여 횟수 체크 → 차감 → 서버 권한에서 Fire() 호출
	virtual void OnUse_Implementation() override;

	// [WEAPON-007] 실제 발사 로직. 자식이 override (테이저=트레이스, 칼=콜라이더 등)
	virtual void Fire();

private:
	// [WEAPON-008] 클라이언트에서 눌렀을 때 서버로 발사 위임
	UFUNCTION(Server, Reliable)
	void ServerFire();

protected:
#pragma endregion

#pragma region [WEAPON] 설정값
	// 이 무기가 노리는 진영 (피아식별)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	EWeaponTargetTeam TargetTeam = EWeaponTargetTeam::Enemy;

	// 근접/원거리 명중 방식
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	EWeaponHitMode HitMode = EWeaponHitMode::Ranged;
#pragma endregion

#pragma region [WEAPON] 근접 콜라이더
	// 근접 타격용 콜라이더. BP에서 크기/위치 조정 가능. 평소 꺼두고 휘두를 때만 켬.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Melee")
	TObjectPtr<UBoxComponent> MeleeCollider;

	// [WEAPON-001] 근접 콜라이더 오버랩 on/off (휘두르기 시작/끝에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Weapon|Melee")
	void EnableMeleeCollision(bool bEnable);

	// [WEAPON-002] 근접 콜라이더 오버랩 콜백 - 진영 확인 후 ApplyWeaponHit
	UFUNCTION()
	void OnMeleeOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
#pragma endregion

#pragma region [WEAPON] 원거리 (즉발 트레이스)
	// [WEAPON-003] 즉발 트레이스 발사. TargetTeam 채널로 트레이스해 피아식별. 맞으면 ApplyWeaponHit.
	// 반환: 명중했으면 true, HitResult 채워짐
	UFUNCTION(BlueprintCallable, Category = "Weapon|Ranged")
	bool FireHitscan(const FVector& Start, const FVector& End, FHitResult& OutHit);
#pragma endregion

#pragma region [WEAPON] 공통 히트 처리
	// [WEAPON-004] 실제 무기 효과 (기절/데미지 등). 자식이 override.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon")
	void ApplyWeaponHit(AActor* HitActor, const FHitResult& Hit);
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit);
#pragma endregion

protected:
	// [WEAPON-005] TargetTeam을 실제 트레이스 채널(ECollisionChannel)로 변환
	ECollisionChannel GetTargetTraceChannel() const;

// ---------------------------------------------------------------------------
// [WEAPON] 원거리 발사체 (Projectile) - 현재 미사용, 주석 처리.
//   나중에 날아가는 투사체가 필요하면 아래 주석을 풀고 사용.
//   (별도 Projectile 액터 클래스 AProjectileBase가 필요함)
// ---------------------------------------------------------------------------
// public:
//	// 발사할 투사체 클래스 (BP에서 지정)
//	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Projectile")
//	TSubclassOf<AActor> ProjectileClass;
//
//	// 투사체 발사 속도 (cm/s)
//	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Projectile")
//	float ProjectileSpeed = 3000.0f;
//
//	// [WEAPON-006] 투사체 스폰 후 발사 (서버 권한에서 호출 권장)
//	UFUNCTION(BlueprintCallable, Category = "Weapon|Projectile")
//	AActor* FireProjectile(const FVector& SpawnLocation, const FVector& Direction);
};
