#include "Item/MapItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
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
}

void AMapItem::BeginPlay()
{
	Super::BeginPlay();

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
}

void AMapItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머 정리 (댕글링 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FogUpdateTimerHandle);
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
				MapWidgetInstance->AddToViewport();
				bMapOpen = true;
			}
		}
	}
}

// [MAP-002] 손에 들었을 때: 소지자 세팅 + Fog 갱신 타이머 시작
void AMapItem::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);

	HoldingPlayer = Equipper;

	// Fog 마스크를 주기적으로 갱신하는 타이머 시작 (Tick 대신 타이머)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FogUpdateTimerHandle, this, &AMapItem::UpdateFogMask,
			FogUpdateInterval, true);
	}
}

// [MAP-003] 손에서 해제될 때: 소지자 해제 + 타이머 정지
void AMapItem::OnUnequipped_Implementation(AActor* Equipper)
{
	// 갱신 타이머 정지
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FogUpdateTimerHandle);
	}

	HoldingPlayer = nullptr;

	Super::OnUnequipped_Implementation(Equipper);
}

// [MAP-004] 월드 좌표 → 지도 UV(0~1). 위젯 마커용
FVector2D AMapItem::WorldToMapUV(const FVector& WorldLocation) const
{
	// (월드 XY - 원점) / 전체 크기 → 0~1 정규화
	const float U = (MapWorldSize.X != 0.0f)
		? (WorldLocation.X - MapWorldOrigin.X) / MapWorldSize.X : 0.0f;
	const float V = (MapWorldSize.Y != 0.0f)
		? (WorldLocation.Y - MapWorldOrigin.Y) / MapWorldSize.Y : 0.0f;

	return FVector2D(FMath::Clamp(U, 0.0f, 1.0f), FMath::Clamp(V, 0.0f, 1.0f));
}

// [CAP-001] 캡처 카메라를 소지 플레이어 위로 이동/정렬
void AMapItem::UpdateCaptureTransform()
{
	if (!CaptureCamera || !HoldingPlayer)
	{
		return;
	}

	// 소지 플레이어 위 CaptureHeight 높이에서 수직 하향으로 정렬
	const FVector PlayerLoc = HoldingPlayer->GetActorLocation();
	const FVector CamLoc = PlayerLoc + FVector(0.0f, 0.0f, CaptureHeight);
	CaptureCamera->SetWorldLocation(CamLoc);
	CaptureCamera->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
}

// [FOG-001] 소지 플레이어 위치를 Fog 마스크에 누적으로 밝힘
void AMapItem::UpdateFogMask()
{
	if (!HoldingPlayer || !FogMaskRT || !FogBrushMID)
	{
		return;
	}

	// 캡처 카메라를 현재 플레이어 위로 정렬 후 1회 캡처
	UpdateCaptureTransform();
	if (CaptureCamera)
	{
		CaptureCamera->CaptureScene();
	}

	// 소지 플레이어 위치를 지도 UV로 변환하여 브러시 머티리얼에 전달
	const FVector2D PlayerUV = WorldToMapUV(HoldingPlayer->GetActorLocation());
	FogBrushMID->SetVectorParameterValue(
		TEXT("BrushCenter"), FLinearColor(PlayerUV.X, PlayerUV.Y, 0.0f, 0.0f));

	// 밝힘 반경을 UV 비율로 환산 (맵 가로 기준)
	const float RadiusUV = (MapWorldSize.X != 0.0f) ? (RevealRadius / MapWorldSize.X) : 0.0f;
	FogBrushMID->SetScalarParameterValue(TEXT("BrushRadius"), RadiusUV);

	// 마스크에 덧그리기(누적) — 기존 밝힘을 지우지 않도록 머티리얼 블렌드로 누적 처리
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FogMaskRT, FogBrushMID);
}
