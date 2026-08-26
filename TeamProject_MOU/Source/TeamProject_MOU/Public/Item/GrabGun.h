#pragma once

#include "CoreMinimal.h"
#include "Item/WeaponItemBase.h"
#include "GrabGun.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class ACharacterBase;
class UGrabFollowComponent;

/**
 * AGrabGun
 * 전투/유틸 아이템 - 그래버 건 (팬터그래프 집게총).
 * 좌클릭(OnUse) 시 카메라 조준 방향으로 피아식별 트레이스를 발사해 맞은 캐릭터를
 * 그 캐릭터에 붙어있는 UGrabFollowComponent를 통해 "집는다"(따라오게 만든다).
 *
 * 놓기 방식: 재발사 토글. (공용 입력 코드 수정 없이 OnUse 단발만으로 잡기/놓기)
 *   - 잡고 있지 않을 때 좌클릭 → 트레이스해서 잡음
 *   - 잡고 있을 때  좌클릭 → 잡은 대상을 놓음
 *
 * 연출(B층): body_shell(=루트 MeshComponent)에 팬터그래프 부품 메시들을 계층으로 붙이고,
 *   ExtendAlpha(0=접힘~1=최대뻗음)를 삼각함수로 풀어 링크가 촤르륵 뻗었다 접히게 한다.
 *   전기 이펙트류 VFX는 블루프린트에서 BlueprintImplementableEvent로 처리.
 */
UCLASS()
class TEAMPROJECT_MOU_API AGrabGun : public AWeaponItemBase
{
	GENERATED_BODY()

public:
	AGrabGun();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#pragma region [GRAB] 컴포넌트
	// 팬터그래프 링크가 매달리는 루트. body_shell(=MeshComponent) 앞쪽에 붙는다.
	// BP에서 위치를 총구 앞으로 옮겨두면 링크가 그 지점에서 뻗어나간다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GrabGun")
	TObjectPtr<USceneComponent> LinkageRoot;

	// 총구/집게 끝 지점. VFX 시작 위치 및 트레이스 기준 폴백에 사용.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GrabGun")
	TObjectPtr<USceneComponent> MuzzlePoint;
#pragma endregion

#pragma region [GRAB] 설정값
	// 트레이스 사거리 (cm) - 카메라 시점 기준 전방
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	float GrabRange = 800.0f;

	// 집을 때 잡은 대상을 어느 소켓에 매달지 (GrabFollowComponent로 전달)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FName GrabSocketName = TEXT("GrabSocket");

	// 집은 대상과의 상대 오프셋 (소켓 기준). 잡은 대상을 앞쪽에 띄워두고 싶을 때 조정.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun")
	FVector GrabRelativeOffset = FVector::ZeroVector;
#pragma endregion

#pragma region [GRAB] 팬터그래프 연출 설정 (GLB matrix 실측 기반)
	// 셀(가위 링크) 개수. 임포트된 bar_up_0..8 / pin_*_0..8 기준 9칸.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	int32 CellCount = 9;

	// 링크 반팔 길이 R (cm). GLB 실측: bar가 pivot 자식으로 (R,0,±0.35)에 놓임 = 3.1.
	//   셀 간격 dx = 2*R*cos(a), 핀 반높이 hh = R*sin(a).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float LinkHalfLength = 3.1f;

	// linkage_root 로컬 위치 (cm). GLB 실측 (7.0, 1.1, 0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector LinkageRootOffset = FVector(7.0f, 1.1f, 0.0f);

	// bar 메시가 pivot 자식으로 갖는 Z 오프셋 (cm). GLB 실측 ±0.35.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float BarZOffset = 0.35f;

	// 펼침(최대 뻗음) 각 a (deg). GLB 기본 포즈 실측 = 35.98°.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float ExtendedAngleDeg = 35.98f;

	// 접힘 각 a (deg). 각이 클수록 셀이 촘촘히 접힘. 기본 70°(접었을 때 짧아짐).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float FoldedAngleDeg = 70.0f;

	// 링크가 뻗는/접히는 속도 (alpha/초). 1이면 1초에 완전히 뻗음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float ExtendSpeed = 4.0f;

	// 집게턱 위치 (end_yoke 자식). GLB 실측 (0.6, ±2.62, 0), 기본 회전 ±10.03°.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector JawPivotOffset = FVector(0.6f, 2.62f, 0.0f);

	// 집게턱 기본(펼침) 회전 (deg). GLB 실측 top=+10.03, bottom=-10.03.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float JawBaseAngleDeg = 10.03f;

	// 집게턱 추가 벌림 각 (deg). alpha에 비례해 기본각(10.03°)에서 더 벌어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float JawOpenAngle = 8.0f;

	// --- 집게 메시 방향 미세조정 (뷰포트 보며 Details에서 돌린다) ---
	// blade 메시의 pivot 로컬 위치/회전. GLB 실측 위치 (2.766, ∓0.965), 회전 0.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FVector JawBladeOffset = FVector(2.766f, 0.965f, 0.0f);

	// blade 메시 회전 (deg). 방향이 틀어지면 여기를 돌린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	float JawBladeYaw = 0.0f;

	// pad 메시의 pivot 로컬 위치. GLB 실측 (4.6, ∓2.15).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	FVector JawPadOffset = FVector(4.6f, 2.15f, 0.0f);

	// pad 메시 회전 (deg). GLB 실측 ±35.52. 방향이 틀어지면 여기를 돌린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Jaw")
	float JawPadYaw = 35.52f;

	// 트리거 피벗 위치 (body_shell 자식). GLB 실측 (-0.2, -0.8, 0).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	FVector TriggerPivotOffset = FVector(-0.2f, -0.8f, 0.0f);

	// 트리거 최대 당김 각 (deg).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GrabGun|Linkage")
	float TriggerPullAngle = 24.0f;
#pragma endregion

#pragma region [GRAB] 사용/발사 (WeaponItemBase 훅)
	// [GRAB-001] 발사 override: 잡고 있으면 놓기, 아니면 트레이스해서 잡기 (재발사 토글)
	virtual void Fire() override;

	// [GRAB-002] 잡는 순간에만 내구도 1 소모. (놓기는 소모 안 함 → Fire 안에서 조건부 처리)
	//   토글이라 발사 시점 자동차감을 끄고, 실제 잡기 성공 시에만 서버에서 수동 차감한다.
	virtual bool ShouldConsumeUseOnFire() const override { return false; }

	// [GRAB-003] 무기 공통 히트 처리 override: 맞은 캐릭터를 GrabFollowComponent로 집기
	virtual void ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit) override;
#pragma endregion

#pragma region [GRAB] 소유권/생명주기
	// [GRAB-004] 놓을 때(G키) 잡고 있던 대상도 자동 해제
	virtual void Drop_Implementation(FVector DropLocation, AActor* Dropper = nullptr) override;

	// [GRAB-005] 던질 때도 잡고 있던 대상 해제
	virtual void Throw_Implementation(FVector ThrowVelocity, AActor* Thrower = nullptr) override;
#pragma endregion

#pragma region [GRAB] 연출 훅 (Blueprint VFX)
	// [GRAB-006] 발사 이펙트 훅 (집게 발사/명중 등). 시작/끝 지점 전달, 모든 클라 재생
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayFireEffect(FVector Start, FVector End, bool bHit);

	// 블루프린트에서 실제 나이아가라/케이블 VFX를 붙이는 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "GrabGun|FX")
	void OnFireEffect(FVector Start, FVector End, bool bHit);
#pragma endregion

private:
#pragma region [GRAB] 잡기 상태
	// 현재 이 그래버가 집고 있는 대상. 서버 권한에서만 갱신, 재발사 토글 판단에 사용.
	UPROPERTY(Replicated)
	TObjectPtr<ACharacterBase> GrabbedTarget;

	// [GRAB-010] 대상을 집는다 (서버). 성공 시 내구도 1 소모.
	void GrabTarget(ACharacterBase* Target);

	// [GRAB-011] 잡고 있던 대상을 놓는다 (서버).
	void ReleaseTarget();
#pragma endregion

#pragma region [GRAB] 팬터그래프 부품/구동
	// 링크 뻗음 정도 목표치 (0=접힘, 1=최대뻗음). 잡으면 1, 놓으면 0으로 보간된다.
	UPROPERTY(Replicated)
	float TargetExtendAlpha = 0.0f;

	// 현재 뻗음 정도 (매 Tick TargetExtendAlpha로 보간). 시각 표현용이라 각 클라에서 로컬 계산.
	float CurrentExtendAlpha = 0.0f;

	// 셀별 회전 피벗(빈 SceneComponent)과 그 아래 bar 메시. 인덱스=셀 번호.
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> CellPivots;       // cell_i 기준점 (linkage_root 자식으로 체인)
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> PivotUp;           // pivot_up_i
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> PivotDn;           // pivot_dn_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinTop;       // pin_top_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinBottom;    // pin_bottom_i
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PinCenter;    // pin_center_i

	// 끝단 요크 + 집게턱 + 트리거
	UPROPERTY()
	TObjectPtr<USceneComponent> EndYoke;
	UPROPERTY()
	TObjectPtr<USceneComponent> JawPivotTop;
	UPROPERTY()
	TObjectPtr<USceneComponent> JawPivotBottom;
	UPROPERTY()
	TObjectPtr<USceneComponent> TriggerPivot;

	// 집게 blade/pad 메시 (Details 값으로 위치·회전 갱신하려고 포인터 보관)
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawBladeTop;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawBladeBottom;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawPadTop;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> JawPadBottom;

	// [GRAB-012] 생성자에서 부품 메시를 계층으로 스폰 (AddComponent 루프)
	void BuildLinkageComponents();

	// [GRAB-013] CurrentExtendAlpha에 맞춰 링크/집게/트리거 트랜스폼 갱신 (Tick에서 호출)
	void UpdateLinkagePose(float Alpha);

	// [GRAB-014] 부품 메시 하나 로드 헬퍼 (extending-arm-toy-gun 폴더 기준)
	UStaticMesh* LoadPartMesh(const FString& PartName) const;

	// GLB matrix 실측 좌표를 UE 로컬 위치 벡터로 만든다.
	FVector MakeLocal(float X, float Y, float Z) const;
	// Z축(Yaw) 회전을 UE 로컬 회전으로 만든다.
	FRotator MakeYawRot(float DegZ) const;
#pragma endregion
};
