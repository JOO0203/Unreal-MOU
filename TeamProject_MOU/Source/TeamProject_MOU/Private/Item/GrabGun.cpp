#include "Item/GrabGun.h"
#include "Base/CharacterBase.h"
#include "Components/GrabFollowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h" // [DEBUG-GRAB] 확인용 임시

// 부품 메시들이 들어있는 폴더 (재임포트 위치)
static const TCHAR* GPartMeshDir =
	TEXT("/Game/04_JJO/Asset/StaticMeshes/GrabGun/StaticMeshes/");

// 재임포트 시 Interchange가 부품 이름을 버리고 mesh 인덱스로만 저장했다.
// (파일명: extending-arm-toy-gun2222_mesh_<idx>) 그래서 부품 이름 → mesh 인덱스 매핑이 필요하다.
// 매핑은 GLB(extending-arm-toy-gun2222.glb)의 node.mesh 참조 순서에서 추출한 실측값이다.
static const TCHAR* GMeshAssetPrefix = TEXT("extending-arm-toy-gun2222_mesh_");

static int32 PartNameToMeshIndex(const FString& PartName)
{
	static const TMap<FString, int32> Map = {
		{ TEXT("body_shell"), 0 },
		{ TEXT("side_plate_left"), 1 },
		{ TEXT("vent_slat_l0"), 2 }, { TEXT("vent_slat_l1"), 3 }, { TEXT("vent_slat_l2"), 4 },
		{ TEXT("vent_slat_l3"), 5 }, { TEXT("vent_slat_l4"), 6 },
		{ TEXT("screw_l0"), 7 }, { TEXT("screw_l1"), 8 }, { TEXT("screw_l2"), 9 },
		{ TEXT("screw_l3"), 10 }, { TEXT("screw_l4"), 11 },
		{ TEXT("side_plate_right"), 12 },
		{ TEXT("vent_slat_r0"), 13 }, { TEXT("vent_slat_r1"), 14 }, { TEXT("vent_slat_r2"), 15 },
		{ TEXT("vent_slat_r3"), 16 }, { TEXT("vent_slat_r4"), 17 },
		{ TEXT("screw_r0"), 18 }, { TEXT("screw_r1"), 19 }, { TEXT("screw_r2"), 20 },
		{ TEXT("screw_r3"), 21 }, { TEXT("screw_r4"), 22 },
		{ TEXT("trigger_guard"), 23 }, { TEXT("trigger"), 24 }, { TEXT("trigger_pivot_pin"), 25 },
		{ TEXT("muzzle_anchor"), 26 },
		{ TEXT("bar_up_0"), 27 }, { TEXT("bar_dn_0"), 28 }, { TEXT("pin_top_0"), 29 }, { TEXT("pin_bottom_0"), 30 }, { TEXT("pin_center_0"), 31 },
		{ TEXT("bar_up_1"), 32 }, { TEXT("bar_dn_1"), 33 }, { TEXT("pin_top_1"), 34 }, { TEXT("pin_bottom_1"), 35 }, { TEXT("pin_center_1"), 36 },
		{ TEXT("bar_up_2"), 37 }, { TEXT("bar_dn_2"), 38 }, { TEXT("pin_top_2"), 39 }, { TEXT("pin_bottom_2"), 40 }, { TEXT("pin_center_2"), 41 },
		{ TEXT("bar_up_3"), 42 }, { TEXT("bar_dn_3"), 43 }, { TEXT("pin_top_3"), 44 }, { TEXT("pin_bottom_3"), 45 }, { TEXT("pin_center_3"), 46 },
		{ TEXT("bar_up_4"), 47 }, { TEXT("bar_dn_4"), 48 }, { TEXT("pin_top_4"), 49 }, { TEXT("pin_bottom_4"), 50 }, { TEXT("pin_center_4"), 51 },
		{ TEXT("bar_up_5"), 52 }, { TEXT("bar_dn_5"), 53 }, { TEXT("pin_top_5"), 54 }, { TEXT("pin_bottom_5"), 55 }, { TEXT("pin_center_5"), 56 },
		{ TEXT("bar_up_6"), 57 }, { TEXT("bar_dn_6"), 58 }, { TEXT("pin_top_6"), 59 }, { TEXT("pin_bottom_6"), 60 }, { TEXT("pin_center_6"), 61 },
		{ TEXT("bar_up_7"), 62 }, { TEXT("bar_dn_7"), 63 }, { TEXT("pin_top_7"), 64 }, { TEXT("pin_bottom_7"), 65 }, { TEXT("pin_center_7"), 66 },
		{ TEXT("bar_up_8"), 67 }, { TEXT("bar_dn_8"), 68 }, { TEXT("pin_top_8"), 69 }, { TEXT("pin_bottom_8"), 70 }, { TEXT("pin_center_8"), 71 },
		{ TEXT("yoke_spine"), 72 },
		{ TEXT("jaw_blade_top"), 73 }, { TEXT("jaw_pad_top"), 74 }, { TEXT("jaw_pin_top"), 75 },
		{ TEXT("jaw_blade_bottom"), 76 }, { TEXT("jaw_pad_bottom"), 77 }, { TEXT("jaw_pin_bottom"), 78 },
	};
	const int32* Found = Map.Find(PartName);
	return Found ? *Found : INDEX_NONE;
}

AGrabGun::AGrabGun()
{
	PrimaryActorTick.bCanEverTick = true;

	MaxDurability = 100.0f;
	CurrentDurability = MaxDurability;

	// 루트(MeshComponent, ItemBase 소유) = body_shell 총몸
	if (MeshComponent)
	{
		if (UStaticMesh* Body = LoadPartMesh(TEXT("body_shell")))
		{
			MeshComponent->SetStaticMesh(Body);
		}
	}

	// 링크가 매달리는 루트 + 총구 지점 (GLB 실측 linkage_root = 7.0, 1.1, 0)
	LinkageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LinkageRoot"));
	LinkageRoot->SetupAttachment(MeshComponent);
	LinkageRoot->SetRelativeLocation(LinkageRootOffset);

	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshComponent);

	// 팬터그래프 부품 계층 조립
	BuildLinkageComponents();

	// 생성자에서 바로 접힘 포즈(alpha=0)를 적용해 편집 뷰포트에서도 접혀 보이게 한다.
	CurrentExtendAlpha = 0.0f;
	TargetExtendAlpha = 0.0f;
	UpdateLinkagePose(0.0f);
}

// BP에서 Linkage 설정값(각도/길이 등)을 바꿀 때마다 편집 뷰포트에 접힘 포즈를 다시 그린다.
void AGrabGun::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateLinkagePose(CurrentExtendAlpha);
}

void AGrabGun::BeginPlay()
{
	Super::BeginPlay();

	// 시작은 완전히 접힌 포즈
	CurrentExtendAlpha = 0.0f;
	TargetExtendAlpha = 0.0f;
	UpdateLinkagePose(0.0f);
}

void AGrabGun::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrabGun, GrabbedTarget);
	DOREPLIFETIME(AGrabGun, TargetExtendAlpha);
}

// =============================================================================
// [GRAB] 부품 메시 로드 / 축 헬퍼
// =============================================================================

UStaticMesh* AGrabGun::LoadPartMesh(const FString& PartName) const
{
	// 부품 이름 → mesh 인덱스 → 실제 에셋명(extending-arm-toy-gun2222_mesh_<idx>)
	const int32 Idx = PartNameToMeshIndex(PartName);
	if (Idx == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Unknown part name: %s"), *PartName);
		return nullptr;
	}
	const FString AssetName = FString(GMeshAssetPrefix) + FString::FromInt(Idx);
	// 예: /Game/.../GrabGun/StaticMeshes/extending-arm-toy-gun2222_mesh_27.extending-arm-toy-gun2222_mesh_27
	const FString Path = FString(GPartMeshDir) + AssetName + TEXT(".") + AssetName;
	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path));
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Failed to load mesh for %s at %s"), *PartName, *Path);
	}
	return Mesh;
}

FVector AGrabGun::MakeLocal(float X, float Y, float Z) const
{
	// GLB matrix 실측값을 그대로 UE 로컬로 사용한다. (Interchange가 축 변환 처리)
	return FVector(X, Y, Z);
}

FRotator AGrabGun::MakeYawRot(float DegZ) const
{
	// GLB 노드 회전은 Z축(Yaw) 기준. 가위 링크가 XY 평면에서 접힌다.
	return FRotator(0.0f, DegZ, 0.0f);
}

// =============================================================================
// [GRAB-012] 팬터그래프 부품 계층 스폰
// =============================================================================

void AGrabGun::BuildLinkageComponents()
{
	// 헬퍼: SceneComponent(빈 피벗) 생성
	auto MakeScene = [this](const FName& Name, USceneComponent* Parent, const FVector& Loc, const FRotator& Rot) -> USceneComponent*
	{
		USceneComponent* Comp = CreateDefaultSubobject<USceneComponent>(Name);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(Loc);
		Comp->SetRelativeRotation(Rot);
		return Comp;
	};

	// 헬퍼: StaticMesh 부품 생성 (PartName으로 메시 로드 + 부착)
	auto MakeMesh = [this](const FName& CompName, const FString& PartName, USceneComponent* Parent, const FVector& Loc, const FRotator& Rot) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(CompName);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(Loc);
		Comp->SetRelativeRotation(Rot);
		if (UStaticMesh* M = LoadPartMesh(PartName))
		{
			Comp->SetStaticMesh(M);
		}
		// 링크 부품은 순수 비주얼. 충돌/물리 끔 (루트 body_shell만 물리).
		Comp->SetSimulatePhysics(false);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Comp;
	};

	CellPivots.Reset();
	PivotUp.Reset();
	PivotDn.Reset();
	PinTop.Reset();
	PinBottom.Reset();
	PinCenter.Reset();

	// cell 체인: cell_00은 linkage_root 직속, cell_01..은 앞 셀의 자식.
	// (초기 위치는 어차피 UpdateLinkagePose가 alpha로 덮어쓰므로 identity로 둔다)
	USceneComponent* PrevCell = LinkageRoot;
	for (int32 i = 0; i < CellCount; ++i)
	{
		USceneComponent* Cell = MakeScene(*FString::Printf(TEXT("cell_%02d"), i), PrevCell, FVector::ZeroVector, FRotator::ZeroRotator);
		CellPivots.Add(Cell);

		// pivot_up_i / pivot_dn_i (빈 피벗). 위치/회전은 Update에서 hh, a로 갱신.
		USceneComponent* PUp = MakeScene(*FString::Printf(TEXT("pivot_up_%d"), i), Cell,
			FVector::ZeroVector, FRotator::ZeroRotator);
		USceneComponent* PDn = MakeScene(*FString::Printf(TEXT("pivot_dn_%d"), i), Cell,
			FVector::ZeroVector, FRotator::ZeroRotator);
		PivotUp.Add(PUp);
		PivotDn.Add(PDn);

		// bar 메시 (피벗 자식). GLB 실측: (R, 0, ±BarZOffset). X=R이 회전 반지름.
		MakeMesh(*FString::Printf(TEXT("bar_up_%d"), i), FString::Printf(TEXT("bar_up_%d"), i), PUp,
			MakeLocal(LinkHalfLength, 0.0f, +BarZOffset), FRotator::ZeroRotator);
		MakeMesh(*FString::Printf(TEXT("bar_dn_%d"), i), FString::Printf(TEXT("bar_dn_%d"), i), PDn,
			MakeLocal(LinkHalfLength, 0.0f, -BarZOffset), FRotator::ZeroRotator);

		// 핀 3개 (cell 직속). top/bottom은 hh, center는 dx/2로 Update에서 갱신.
		PinTop.Add(MakeMesh(*FString::Printf(TEXT("pin_top_%d"), i), FString::Printf(TEXT("pin_top_%d"), i), Cell,
			FVector::ZeroVector, FRotator::ZeroRotator));
		PinBottom.Add(MakeMesh(*FString::Printf(TEXT("pin_bottom_%d"), i), FString::Printf(TEXT("pin_bottom_%d"), i), Cell,
			FVector::ZeroVector, FRotator::ZeroRotator));
		PinCenter.Add(MakeMesh(*FString::Printf(TEXT("pin_center_%d"), i), FString::Printf(TEXT("pin_center_%d"), i), Cell,
			FVector::ZeroVector, FRotator::ZeroRotator));

		PrevCell = Cell;
	}

	// end_yoke (마지막 셀 자식). 위치는 Update에서 dx로 갱신.
	// GLB 계층: end_yoke > yoke_spine_holder(identity) > yoke_spine(mesh)
	EndYoke = MakeScene(TEXT("end_yoke"), PrevCell, FVector::ZeroVector, FRotator::ZeroRotator);
	USceneComponent* YokeHolder = MakeScene(TEXT("yoke_spine_holder"), EndYoke, FVector::ZeroVector, FRotator::ZeroRotator);
	MakeMesh(TEXT("yoke_spine"), TEXT("yoke_spine"), YokeHolder, FVector::ZeroVector, FRotator::ZeroRotator);

	// 집게턱 (GLB 실측 계층 그대로):
	//   jaw_pivot_* (0.6, ±2.62, 회전 ±10.03°)  ← 벌림 회전은 여기(Update에서 갱신)
	//     jaw_reach_* (identity)
	//       jaw_blade_* (2.766, ∓0.965)
	//       jaw_pad_*   (4.600, ∓2.150, 회전 ±35.52°)
	//     jaw_pin_* (identity)
	// TOP
	JawPivotTop = MakeScene(TEXT("jaw_pivot_top"), EndYoke,
		MakeLocal(0.6f, +2.62f, 0.0f), MakeYawRot(+10.03f));
	{
		USceneComponent* Reach = MakeScene(TEXT("jaw_reach_top"), JawPivotTop, FVector::ZeroVector, FRotator::ZeroRotator);
		JawBladeTop = MakeMesh(TEXT("jaw_blade_top"), TEXT("jaw_blade_top"), Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		JawPadTop   = MakeMesh(TEXT("jaw_pad_top"),   TEXT("jaw_pad_top"),   Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		MakeMesh(TEXT("jaw_pin_top"),   TEXT("jaw_pin_top"),   JawPivotTop, FVector::ZeroVector, FRotator::ZeroRotator);
	}
	// BOTTOM (Y·회전 부호 반대)
	JawPivotBottom = MakeScene(TEXT("jaw_pivot_bottom"), EndYoke,
		MakeLocal(0.6f, -2.62f, 0.0f), MakeYawRot(-10.03f));
	{
		USceneComponent* Reach = MakeScene(TEXT("jaw_reach_bottom"), JawPivotBottom, FVector::ZeroVector, FRotator::ZeroRotator);
		JawBladeBottom = MakeMesh(TEXT("jaw_blade_bottom"), TEXT("jaw_blade_bottom"), Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		JawPadBottom   = MakeMesh(TEXT("jaw_pad_bottom"),   TEXT("jaw_pad_bottom"),   Reach, FVector::ZeroVector, FRotator::ZeroRotator);
		MakeMesh(TEXT("jaw_pin_bottom"),   TEXT("jaw_pin_bottom"),   JawPivotBottom, FVector::ZeroVector, FRotator::ZeroRotator);
	}

	// 트리거 피벗 (body_shell 직속). GLB 실측 trigger_pivot (-0.2, -0.8, 0), trigger (0.013, -2.468, 0).
	TriggerPivot = MakeScene(TEXT("trigger_pivot"), MeshComponent, MakeLocal(-0.2f, -0.8f, 0.0f), FRotator::ZeroRotator);
	MakeMesh(TEXT("trigger"), TEXT("trigger"), TriggerPivot, MakeLocal(0.013f, -2.468f, 0.0f), FRotator::ZeroRotator);
}

// =============================================================================
// [GRAB-013] 링크 포즈 갱신 (삼각함수)
// =============================================================================

void AGrabGun::UpdateLinkagePose(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

	// alpha: 0=접힘(FoldedAngleDeg, 큰 각) ~ 1=최대뻗음(ExtendedAngleDeg, 작은 각)
	// GLB 실측 관계: dx = 2*R*cos(a), hh = R*sin(a). (R=LinkHalfLength)
	const float aDeg = FMath::Lerp(FoldedAngleDeg, ExtendedAngleDeg, Alpha);
	const float aRad = FMath::DegreesToRadians(aDeg);
	const float R = LinkHalfLength;
	const float dx = 2.0f * R * FMath::Cos(aRad);   // cm, 셀 간 X 간격
	const float hh = R * FMath::Sin(aRad);          // cm, 핀 상하 반높이

	// cell_01..N : X = dx (cell_00은 0 고정). end_yoke도 dx.
	for (int32 i = 1; i < CellPivots.Num(); ++i)
	{
		if (CellPivots[i])
		{
			CellPivots[i]->SetRelativeLocation(MakeLocal(dx, 0.0f, 0.0f));
		}
	}
	if (EndYoke)
	{
		EndYoke->SetRelativeLocation(MakeLocal(dx, 0.0f, 0.0f));
	}

	// 각 셀 내부: pivot_up/dn 위치+회전, 핀 위치 (GLB 실측 부호: pivot_up Y=-hh, 회전=+a)
	for (int32 i = 0; i < CellPivots.Num(); ++i)
	{
		if (PivotUp.IsValidIndex(i) && PivotUp[i])
		{
			PivotUp[i]->SetRelativeLocation(MakeLocal(0.0f, -hh, 0.0f));
			PivotUp[i]->SetRelativeRotation(MakeYawRot(+aDeg));
		}
		if (PivotDn.IsValidIndex(i) && PivotDn[i])
		{
			PivotDn[i]->SetRelativeLocation(MakeLocal(0.0f, +hh, 0.0f));
			PivotDn[i]->SetRelativeRotation(MakeYawRot(-aDeg));
		}
		if (PinTop.IsValidIndex(i) && PinTop[i])
		{
			PinTop[i]->SetRelativeLocation(MakeLocal(0.0f, +hh, 0.0f));
		}
		if (PinBottom.IsValidIndex(i) && PinBottom[i])
		{
			PinBottom[i]->SetRelativeLocation(MakeLocal(0.0f, -hh, 0.0f));
		}
		if (PinCenter.IsValidIndex(i) && PinCenter[i])
		{
			PinCenter[i]->SetRelativeLocation(MakeLocal(dx / 2.0f, 0.0f, 0.0f));
		}
	}

	// 집게턱: 기본각(JawBaseAngleDeg)에서 alpha만큼 더 벌어짐. top=+, bottom=-.
	const float JawDeg = JawBaseAngleDeg + JawOpenAngle * Alpha;
	if (JawPivotTop)
	{
		JawPivotTop->SetRelativeRotation(MakeYawRot(+JawDeg));
	}
	if (JawPivotBottom)
	{
		JawPivotBottom->SetRelativeRotation(MakeYawRot(-JawDeg));
	}

	// 집게 blade/pad 메시 위치·회전 (Details에서 조정 가능). top=+Y/+Yaw, bottom=-Y/-Yaw 대칭.
	if (JawBladeTop)
	{
		JawBladeTop->SetRelativeLocation(MakeLocal(JawBladeOffset.X, +JawBladeOffset.Y, JawBladeOffset.Z));
		JawBladeTop->SetRelativeRotation(MakeYawRot(+JawBladeYaw));
	}
	if (JawBladeBottom)
	{
		JawBladeBottom->SetRelativeLocation(MakeLocal(JawBladeOffset.X, -JawBladeOffset.Y, JawBladeOffset.Z));
		JawBladeBottom->SetRelativeRotation(MakeYawRot(-JawBladeYaw));
	}
	if (JawPadTop)
	{
		JawPadTop->SetRelativeLocation(MakeLocal(JawPadOffset.X, +JawPadOffset.Y, JawPadOffset.Z));
		JawPadTop->SetRelativeRotation(MakeYawRot(+JawPadYaw));
	}
	if (JawPadBottom)
	{
		JawPadBottom->SetRelativeLocation(MakeLocal(JawPadOffset.X, -JawPadOffset.Y, JawPadOffset.Z));
		JawPadBottom->SetRelativeRotation(MakeYawRot(-JawPadYaw));
	}
	// 트리거 당김: t=alpha
	if (TriggerPivot)
	{
		TriggerPivot->SetRelativeRotation(MakeYawRot(TriggerPullAngle * Alpha));
	}
}

void AGrabGun::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 목표치로 부드럽게 보간 (시각 표현이라 서버/클라 각자 로컬 계산)
	if (!FMath::IsNearlyEqual(CurrentExtendAlpha, TargetExtendAlpha, 0.001f))
	{
		CurrentExtendAlpha = FMath::FInterpConstantTo(CurrentExtendAlpha, TargetExtendAlpha, DeltaTime, ExtendSpeed);
		UpdateLinkagePose(CurrentExtendAlpha);
	}
}

// =============================================================================
// [GRAB-001] 발사: 재발사 토글 (잡고 있으면 놓기, 아니면 잡기)
// =============================================================================

void AGrabGun::Fire()
{
	// Fire는 서버 권한에서 호출됨 (WeaponItemBase::TryFireOnServer 경로)
	// 이미 잡고 있으면 → 놓기
	if (GrabbedTarget)
	{
		ReleaseTarget();
		return;
	}

	// 아니면 → 카메라 조준 트레이스로 대상 찾기 (테이저와 동일 방식)
	FVector ViewLocation = GetActorLocation();
	FRotator ViewRotation = GetActorRotation();

	APawn* OwnerPawn = Cast<APawn>(LastOwner);
	if (OwnerPawn && OwnerPawn->GetController())
	{
		OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else if (LastOwner)
	{
		LastOwner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + ViewRotation.Vector() * GrabRange;

	// 피아식별 트레이스는 부모(WeaponItemBase)에 위임. 맞으면 ApplyWeaponHit(=집기) 내부 호출.
	FHitResult Hit;
	const bool bHit = FireHitscan(TraceStart, TraceEnd, Hit);

	// [DEBUG-GRAB] 트레이스 시각화 - 확인 후 제거
	DrawDebugLine(GetWorld(), TraceStart, TraceEnd, bHit ? FColor::Green : FColor::Red, false, 2.0f, 0, 1.5f);
	if (bHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Hit: %s"), *GetNameSafe(Hit.GetActor()));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Miss"));
	}

	// VFX: 총구 → 명중지점(또는 최대거리)
	const FVector FxStart = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : TraceStart;
	const FVector FxEnd = bHit ? Hit.ImpactPoint : TraceEnd;
	MulticastPlayFireEffect(FxStart, FxEnd, bHit);
}

// [GRAB-003] 히트 처리: 맞은 캐릭터를 GrabFollowComponent로 집기
void AGrabGun::ApplyWeaponHit_Implementation(AActor* HitActor, const FHitResult& Hit)
{
	ACharacterBase* Target = Cast<ACharacterBase>(HitActor);
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Hit but NOT ACharacterBase: %s"), *GetNameSafe(HitActor));
		return;
	}
	GrabTarget(Target);
}

// [GRAB-010] 대상을 집는다 (서버)
void AGrabGun::GrabTarget(ACharacterBase* Target)
{
	if (!HasAuthority() || !Target)
	{
		return;
	}

	// 잡히는 대상 몸에 붙어있는 GrabFollowComponent를 찾아 시작시킴
	UGrabFollowComponent* Follow = Target->FindComponentByClass<UGrabFollowComponent>();
	if (!Follow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Target has NO GrabFollowComponent: %s"), *GetNameSafe(Target));
		return;
	}

	ACharacter* Carrier = Cast<ACharacter>(LastOwner); // 이 그래버를 든 플레이어
	if (!Carrier)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Owner is not a Character, cannot grab"));
		return;
	}

	Follow->StartGrabFollow(Carrier, GrabSocketName, GrabRelativeOffset, true);
	GrabbedTarget = Target;

	// 잡기 성공 시에만 내구도 1 소모 (ShouldConsumeUseOnFire=false라 여기서 수동 차감)
	if (CurrentDurability > 0.0f)
	{
		CurrentDurability -= 1.0f;
	}

	// 링크 뻗음 (모든 클라 복제)
	TargetExtendAlpha = 1.0f;

	UE_LOG(LogTemp, Warning, TEXT("[GRAB] Grabbed %s"), *GetNameSafe(Target));
}

// [GRAB-011] 잡고 있던 대상을 놓는다 (서버)
void AGrabGun::ReleaseTarget()
{
	if (!HasAuthority())
	{
		return;
	}

	if (GrabbedTarget)
	{
		if (UGrabFollowComponent* Follow = GrabbedTarget->FindComponentByClass<UGrabFollowComponent>())
		{
			Follow->StopGrabFollow();
		}
		UE_LOG(LogTemp, Warning, TEXT("[GRAB] Released %s"), *GetNameSafe(GrabbedTarget));
	}

	GrabbedTarget = nullptr;
	// 링크 접힘
	TargetExtendAlpha = 0.0f;
}

// [GRAB-004] 그래버 자체를 내려놓을 때 잡고 있던 대상도 해제
void AGrabGun::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	if (HasAuthority())
	{
		ReleaseTarget();
	}
	Super::Drop_Implementation(DropLocation, Dropper);
}

// [GRAB-005] 던질 때도 해제
void AGrabGun::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	if (HasAuthority())
	{
		ReleaseTarget();
	}
	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [GRAB-006] 발사 이펙트 훅
void AGrabGun::MulticastPlayFireEffect_Implementation(FVector Start, FVector End, bool bHit)
{
	OnFireEffect(Start, End, bHit);
}
