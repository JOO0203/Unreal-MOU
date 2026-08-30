#include "Item/MapItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"

AMapItem::AMapItem()
{
	PrimaryActorTick.bCanEverTick = false;

	// 지도에 부착되는 top-down 캡처 카메라 (직교 투영, 아래를 향함)
	CaptureCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureCamera"));
	CaptureCamera->SetupAttachment(RootComponent);
	CaptureCamera->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureCamera->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	// 상시 캡처는 부담이 크므로 필요 시점에만 캡처하도록 기본 비활성화
	CaptureCamera->bCaptureEveryFrame = false;
	CaptureCamera->bCaptureOnMovement = false;

	// 캐릭터/적(스켈레탈 메시)을 지도 캡처에서 제외 — 지형만 보이게
	CaptureCamera->ShowFlags.SetSkeletalMeshes(false);

	// 지도 캡처에서 그림자 제거 (평면적인 지형만)
	CaptureCamera->ShowFlags.SetDynamicShadows(false);
}

void AMapItem::BeginPlay()
{
	Super::BeginPlay();

	// [MAP-007] 이 인스턴스 전용 RT 3개를 런타임 생성 (공유 애셋 깜빡임 방지)
	CreatePerInstanceRenderTargets();

	// 레벨 경계 볼륨(MapBounds 태그)에서 지도 범위 자동 설정
	ResolveMapBoundsFromVolume();

	// 캡처 카메라 초기 세팅 (직교 폭 / 렌더타깃 연결)
	if (CaptureCamera)
	{
		CaptureCamera->OrthoWidth = CaptureOrthoWidth;
		CaptureCamera->TextureTarget = CaptureRT;
	}

	// Fog 브러시 동적 머티리얼 인스턴스 생성 (밝힘 원을 그릴 때 사용)
	if (FogBrushMaterial)
	{
		FogBrushMID = UMaterialInstanceDynamic::Create(FogBrushMaterial, this);
	}

	if (UWorld* World = GetWorld())
	{
		// 지형이 로드될 시간을 준 뒤 1회 캡처 (World Partition 스트리밍 대비)
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);   // false = 반복 안 함, 0.5초 후 딱 1번

		// [MAP-002] Fog 갱신은 손에 안 들어도(바닥에서도) 항상 가동
		World->GetTimerManager().SetTimer(
			FogUpdateTimerHandle, this, &AMapItem::UpdateFogMask,
			FogUpdateInterval, true);
	}
}

void AMapItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머 정리 (댕글링 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FogUpdateTimerHandle);
		World->GetTimerManager().ClearTimer(CaptureDelayTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

// [MAP-001] 좌클릭: 전체 지도 오버레이 켜기/끄기 토글
void AMapItem::OnUse_Implementation()
{
	// 위젯 클래스 미지정 시 아무것도 하지 않음
	if (!MapWidgetClass)
	{
		return;
	}

	if (bMapOpen)
	{
		// 열려 있으면 닫기
		if (MapWidgetInstance)
		{
			MapWidgetInstance->RemoveFromParent();
			MapWidgetInstance = nullptr;
		}
		bMapOpen = false;
	}
	else
	{
		// 닫혀 있으면 소지 플레이어의 컨트롤러로 위젯 생성 후 표시
		APawn* OwnerPawn = Cast<APawn>(HoldingPlayer);
		APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
		if (PC)
		{
			MapWidgetInstance = CreateWidget<UUserWidget>(PC, MapWidgetClass);
			if (MapWidgetInstance)
			{
				// [MAP-010] 위젯에 이 지도 인스턴스를 전달 (인스턴스 RT를 참조하게 함, 검은 화면 방지)
				// WBP_Map에 입력 파라미터가 AMapItem* 1개인 커스텀 이벤트 "SetMapItem"을 구현해야 함
				if (UFunction* SetMapFunc = MapWidgetInstance->FindFunction(TEXT("SetMapItem")))
				{
					struct { AMapItem* Map; } Params{ this };
					MapWidgetInstance->ProcessEvent(SetMapFunc, &Params);
				}

				MapWidgetInstance->AddToViewport();
				bMapOpen = true;
			}
		}
	}
}

// [MAP-002] 손에 들었을 때: 소지자만 세팅 (Fog 타이머는 BeginPlay에서 상시 가동)
void AMapItem::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);

	HoldingPlayer = Equipper;
}

// [MAP-003] 손에서 해제될 때: 소지자만 해제 (타이머는 계속 가동 — 바닥에서도 갱신)
void AMapItem::OnUnequipped_Implementation(AActor* Equipper)
{
	HoldingPlayer = nullptr;

	Super::OnUnequipped_Implementation(Equipper);
}

// [MAP-004] 월드 좌표 → 지도 UV(0~1). 위젯 마커용
FVector2D AMapItem::WorldToMapUV(const FVector& WorldLocation) const
{
	// 하향 SceneCapture 이미지 축에 맞춰 정렬:
	// U(가로) = 월드 Y, V(세로) = 1 - 월드 X (위가 +X)
	const float U = (MapWorldSize.Y != 0.0f)
		? (WorldLocation.Y - MapWorldOrigin.Y) / MapWorldSize.Y : 0.0f;
	const float V = (MapWorldSize.X != 0.0f)
		? 1.0f - (WorldLocation.X - MapWorldOrigin.X) / MapWorldSize.X : 0.0f;

	return FVector2D(FMath::Clamp(U, 0.0f, 1.0f), FMath::Clamp(V, 0.0f, 1.0f));
}

// [MAP-005] 레벨의 MapBounds 태그 볼륨을 찾아 Origin/Size 자동 설정
void AMapItem::ResolveMapBoundsFromVolume()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// "MapBounds" 태그가 붙은 액터를 검색
	TArray<AActor*> BoundsActors;
	UGameplayStatics::GetAllActorsWithTag(World, TEXT("MapBounds"), BoundsActors);
	if (BoundsActors.Num() == 0)
	{
		// 볼륨이 없으면 에디터에서 수동 지정한 값을 그대로 사용 (폴백)
		return;
	}

	// 첫 번째 볼륨의 월드 경계 박스를 읽음
	FVector Origin, BoxExtent;
	BoundsActors[0]->GetActorBounds(false, Origin, BoxExtent);

	// 좌하단(XY) = 중심 - 절반크기, 전체크기 = 절반크기 * 2
	MapWorldOrigin = FVector2D(Origin.X - BoxExtent.X, Origin.Y - BoxExtent.Y);
	MapWorldSize = FVector2D(BoxExtent.X * 2.0f, BoxExtent.Y * 2.0f);

	// 캡처가 맵 전체를 담도록 직교 폭을 맵 크기에 맞춤 (가로 기준)
	CaptureOrthoWidth = MapWorldSize.X;
	if (CaptureCamera)
	{
		CaptureCamera->OrthoWidth = CaptureOrthoWidth;
	}
}

// [MAP-006] 레벨 로딩 시 지형을 1회만 캡처 (이후 고정)
void AMapItem::CaptureMapOnce()
{
	// 이미 캡처했으면 다시 안 함 (확실한 1회 보장)
	if (bMapCaptured || !CaptureCamera)
	{
		return;
	}

	// 맵 중앙 상공에 카메라 정렬 후 지형을 1회 캡처
	UpdateCaptureTransform();
	CaptureCamera->TextureTarget = CaptureRT;
	CaptureCamera->CaptureScene();

	bMapCaptured = true;
}

// [CAP-001] 캡처 카메라를 맵 중앙 상공에 고정 (전체 지도 방식)
void AMapItem::UpdateCaptureTransform()
{
	if (!CaptureCamera)
	{
		return;
	}

	// 맵 중앙 XY = 원점 + 크기/2
	const float CenterX = MapWorldOrigin.X + MapWorldSize.X * 0.5f;
	const float CenterY = MapWorldOrigin.Y + MapWorldSize.Y * 0.5f;

	// 맵 중앙 상공에서 수직 하향 (플레이어 위치와 무관하게 고정)
	const FVector CamLoc(CenterX, CenterY, CaptureHeight);
	CaptureCamera->SetWorldLocation(CamLoc);
	CaptureCamera->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
	// 메시(루트) 스케일을 물려받지 않도록 캡처는 항상 1배로 고정 (아이템 크기와 캡처 분리)
	CaptureCamera->SetWorldScale3D(FVector(1.0f));
}

// [FOG-001] 지도 아이템 자신의 위치=밝음(1.0), 지나간 곳=회색(0.5) 누적
void AMapItem::UpdateFogMask()
{
	// (지형 캡처는 CaptureMapOnce에서 1회만 하므로 여기선 캡처하지 않음)

	if (!FogBrushMID)
	{
		return;
	}

	// 시야 기준: 손에 들고 있으면 플레이어 몸 중심(회전 튐 방지), 바닥이면 지도 자신 위치
	const FVector RevealLoc = HoldingPlayer ? HoldingPlayer->GetActorLocation() : GetActorLocation();
	const FVector2D PlayerUV = WorldToMapUV(RevealLoc);
	const float RadiusUV = (MapWorldSize.X != 0.0f) ? (RevealRadius / MapWorldSize.X) : 0.0f;
	FogBrushMID->SetVectorParameterValue(
		TEXT("BrushCenter"), FLinearColor(PlayerUV.X, PlayerUV.Y, 0.0f, 0.0f));
	FogBrushMID->SetScalarParameterValue(TEXT("BrushRadius"), RadiusUV);

	// 1) 방문 마스크: 회색(VisitedIntensity)으로 누적 (지우지 않음)
	if (FogMaskRT)
	{
		FogBrushMID->SetScalarParameterValue(TEXT("BrushIntensity"), VisitedIntensity);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FogMaskRT, FogBrushMID);
	}

	// 2) 현재 마스크: 매번 지우고 현재 위치만 밝게(1.0)
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
		FogBrushMID->SetScalarParameterValue(TEXT("BrushIntensity"), 1.0f);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FogRevealRT, FogBrushMID);
	}
}

// [MAP-007] 인스턴스마다 RT 3개를 런타임 생성 (모든 지도가 같은 캔버스를 공유하던 깜빡임 버그 방지)
void AMapItem::CreatePerInstanceRenderTargets()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 Size = FMath::Max(64, RenderTargetSize);

	// 지형 캡처: 컬러(RGBA8), Fog 마스크: 단일 채널(R8)이면 충분
	CaptureRT   = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_RGBA8);
	FogMaskRT   = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_R8);
	FogRevealRT = UKismetRenderingLibrary::CreateRenderTarget2D(World, Size, Size, RTF_R8);

	// 마스크는 검정(안 밝힘)에서 시작
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::Black);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
	}

	// 캡처 카메라 출력 대상을 이 인스턴스 RT로 연결
	if (CaptureCamera)
	{
		CaptureCamera->TextureTarget = CaptureRT;
	}
}

// [MAP-008] 레벨 이동 시 Fog + 지형 초기화/재캡처 (Seamless Travel 등 아이템이 유지되는 경우 BP에서 호출)
void AMapItem::ResetMapForNewLevel()
{
	// Fog 기록 리셋
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::Black);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::Black);
	}

	// 새 레벨 경계 재인식
	ResolveMapBoundsFromVolume();

	// 지형을 다시 1회 캡처하도록 예약
	bMapCaptured = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);
	}
}
