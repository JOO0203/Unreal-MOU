#include "Item/MapItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Components/BorderSlot.h"
#include "Components/Image.h"
#include "Components/WidgetComponent.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

AMapItem::AMapItem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

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

	// WBP_Map을 화면 UI가 아닌 월드 공간 홀로그램으로 표시합니다.
	HologramWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HologramWidgetComponent"));
	HologramWidgetComponent->SetupAttachment(RootComponent);
	HologramWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	HologramWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HologramWidgetComponent->SetGenerateOverlapEvents(false);
	HologramWidgetComponent->SetTwoSided(true);
	HologramWidgetComponent->SetVisibility(false);
}

void AMapItem::BeginPlay()
{
	Super::BeginPlay();

	// [MAP-014] 시작 시점의 메시 스케일(=BP에서 지정한 값)을 기억. 부착으로 튄 뒤 이 값으로 복원.
	if (MeshComponent)
	{
		InitialMeshScale = MeshComponent->GetRelativeScale3D();
	}

	// [MAP-007] 이 지도 인스턴스 전용 캡처 RT와 WBP_Map 호환용 흰색 마스크 RT를 생성합니다.
	CreatePerInstanceRenderTargets();

	// 레벨 경계 볼륨(MapBounds 태그)에서 지도 범위 자동 설정
	ResolveMapBoundsFromVolume();

	// 캡처 카메라 초기 세팅 (직교 폭 / 인스턴스 렌더타깃 연결)
	if (CaptureCamera)
	{
		CaptureCamera->OrthoWidth = CaptureOrthoWidth;
		CaptureCamera->TextureTarget = CaptureRT;
	}

	if (HologramWidgetComponent)
	{
		HologramWidgetComponent->SetWidgetClass(MapWidgetClass);
		HologramWidgetComponent->SetDrawSize(FVector2D(
			FMath::Max(1.0f, HologramDrawSize.X),
			FMath::Max(1.0f, HologramDrawSize.Y)));
		HologramWidgetComponent->SetRelativeScale3D(HologramRelativeScale);
	}

	if (UWorld* World = GetWorld())
	{
		// 지형이 로드될 시간을 준 뒤 1회 캡처 (World Partition 스트리밍 대비)
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);   // false = 반복 안 함, 0.5초 후 딱 1번
	}
}

// [MAP-013] HoldingPlayer 복제 등록 (서버에서 세팅 → 클라 좌클릭 사용에 필요)
void AMapItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMapItem, HoldingPlayer);
}

void AMapItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseHologram();

	// 타이머 정리 (댕글링 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureDelayTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

// [MAP-017] 홀로그램이 열린 동안 관람자와의 거리 및 홀로그램 방향을 갱신합니다.
void AMapItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bHologramOpen)
	{
		return;
	}

	AActor* Viewer = HologramViewer.Get();
	if (!IsValid(Viewer))
	{
		CloseHologram();
		return;
	}

	const float CloseDistanceSquared = FMath::Square(FMath::Max(0.0f, HologramAutoCloseDistance));
	if (FVector::DistSquared(Viewer->GetActorLocation(), GetActorLocation()) > CloseDistanceSquared)
	{
		CloseHologram();
		return;
	}

	if (HologramWidgetComponent)
	{
		FVector ViewerLocation;
		FRotator ViewerRotation;
		Viewer->GetActorEyesViewPoint(ViewerLocation, ViewerRotation);
		const FVector ToViewer = ViewerLocation - HologramWidgetComponent->GetComponentLocation();
		if (!ToViewer.IsNearlyZero())
		{
			// 홀로그램 컴포넌트의 표시 면이 관람자를 향하도록 현재 뒷면 방향에서 180도 반전합니다.
			HologramWidgetComponent->SetWorldRotation(FRotator(0.0f, ToViewer.Rotation().Yaw, 0.0f));
		}
	}
}

// [MAP-016] F키 상호작용 가능 여부를 확인합니다. 지도는 줍지 않고 홀로그램을 여닫습니다.
bool AMapItem::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(Interactor) && MapWidgetClass != nullptr;
}

// [MAP-018] F키 상호작용 시 해당 플레이어 로컬 화면에서 월드 홀로그램을 토글합니다.
void AMapItem::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	if (bHologramOpen)
	{
		CloseHologram();
	}
	else
	{
		OpenHologram(Interactor);
	}
}

// [MAP-019] 현재 홀로그램 상태에 맞는 F키 안내 문구를 반환합니다.
FText AMapItem::GetInteractPrompt_Implementation() const
{
	return bHologramOpen
		? NSLOCTEXT("Interaction", "CloseMapHologramPrompt", "지도 홀로그램 닫기")
		: NSLOCTEXT("Interaction", "OpenMapHologramPrompt", "지도 홀로그램 열기");
}

// [MAP-022] 지도 장치는 고정형 오브젝트이므로 플레이어·NPC·창고의 모든 집기 경로를 차단합니다.
bool AMapItem::CanBePickedUpBy(AActor* PotentialPicker) const
{
	return false;
}

// [MAP-020] WBP_Map을 월드 위젯에 연결하고 관람자 앞에 홀로그램을 엽니다.
void AMapItem::OpenHologram(AActor* Interactor)
{
	if (!HologramWidgetComponent || !MapWidgetClass || !IsValid(Interactor))
	{
		return;
	}

	if (HologramWidgetComponent->GetWidgetClass() != MapWidgetClass)
	{
		HologramWidgetComponent->SetWidgetClass(MapWidgetClass);
	}

	// 아이템의 방향이나 배치 위치가 아니라, 상호작용한 플레이어의 실제 시선 앞에 홀로그램을 생성합니다.
	FVector ViewerLocation;
	FRotator ViewerRotation;
	Interactor->GetActorEyesViewPoint(ViewerLocation, ViewerRotation);
	constexpr float HologramViewDistance = 150.0f;
	// 시선을 아래로 내려도 홀로그램이 땅속으로 내려가지 않도록 피치는 제외하고 수평 앞 방향만 사용합니다.
	const FVector HorizontalViewDirection = FRotator(0.0f, ViewerRotation.Yaw, 0.0f).Vector();
	const FVector HologramLocation = ViewerLocation
		+ HorizontalViewDirection * HologramViewDistance
		+ FVector::UpVector * HologramHeightOffset;
	HologramWidgetComponent->SetWorldLocation(HologramLocation);

	const FVector ToViewer = ViewerLocation - HologramLocation;
	if (!ToViewer.IsNearlyZero())
	{
		HologramWidgetComponent->SetWorldRotation(FRotator(0.0f, ToViewer.Rotation().Yaw, 0.0f));
	}

	// BeginPlay 직후 캡처가 스트리밍 전의 검은 장면을 담았을 수 있으므로 실제로 열 때 한 번 더 갱신합니다.
	if (CaptureCamera && CaptureRT)
	{
		UpdateCaptureTransform();
		CaptureCamera->TextureTarget = CaptureRT;
		CaptureCamera->CaptureScene();
		bMapCaptured = true;
	}
	HologramViewer = Interactor;
	bHologramOpen = true;
	HologramWidgetComponent->SetVisibility(true);
	SetActorTickEnabled(true);

	RefreshHologramContent();

	// WidgetComponent의 Slate 리소스가 첫 표시 프레임에 만들어지는 경우를 위해 다음 틱에 다시 연결합니다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick([this]()
		{
			if (IsValid(this) && bHologramOpen)
			{
				RefreshHologramContent();
			}
		});
	}
}

// [MAP-021] 월드 홀로그램을 닫고 거리 감시를 중지합니다.
void AMapItem::CloseHologram()
{
	if (HologramWidgetComponent)
	{
		HologramWidgetComponent->SetVisibility(false);
	}

	HologramViewer.Reset();
	bHologramOpen = false;
	SetActorTickEnabled(false);
}

// [MAP-023] Img_Map 표시 머티리얼을 C++에서 한 번 생성하고 캡처 RT와 흰색 호환 마스크를 연결합니다.
void AMapItem::RefreshHologramContent()
{
	if (!HologramWidgetComponent)
	{
		return;
	}

	HologramWidgetComponent->InitWidget();
	UUserWidget* MapWidget = HologramWidgetComponent->GetUserWidgetObject();
	if (!MapWidget)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[MapItem] 홀로그램 WBP_Map 인스턴스를 만들지 못했습니다."));
		return;
	}

	if (UImage* MapImage = Cast<UImage>(MapWidget->GetWidgetFromName(TEXT("Img_Map"))))
	{
		// WBP_Map의 Img_Map은 BorderSlot에서 Center 정렬이라 큰 홀로그램 영역 안에 1100x1100으로만 표시됩니다.
		// 홀로그램에서는 슬롯 전체를 채우도록 늘려 파란 배경 영역과 동일한 크기로 표시합니다.
		if (UBorderSlot* MapBorderSlot = Cast<UBorderSlot>(MapImage->Slot))
		{
			MapBorderSlot->SetHorizontalAlignment(HAlign_Fill);
			MapBorderSlot->SetVerticalAlignment(VAlign_Fill);
			MapBorderSlot->SetPadding(FMargin(0.0f));
		}

		if (!HologramMapMID)
		{
			UMaterialInterface* MapDisplayMaterial = Cast<UMaterialInterface>(MapImage->GetBrush().GetResourceObject());
			if (!MapDisplayMaterial)
			{
				// UE_LOG(LogTemp, Warning, TEXT("[MapItem] Img_Map 브러시에서 지도 표시 머티리얼을 찾지 못했습니다."));
				return;
			}

			HologramMapMID = UMaterialInstanceDynamic::Create(MapDisplayMaterial, this);
		}

		// SceneCapture RT를 텍스처로 직접 그리면 알파 채널 때문에 월드 위젯이 검게 보일 수 있습니다.
		// 기존 지도 표시 머티리얼을 거쳐 RGB를 출력하되 두 마스크는 흰색이라 Fog 구분은 적용되지 않습니다.
		HologramMapMID->SetTextureParameterValue(TEXT("CaptureTex"), CaptureRT);
		HologramMapMID->SetTextureParameterValue(TEXT("FogMaskTex"), FogMaskRT);
		HologramMapMID->SetTextureParameterValue(TEXT("FogRevealTex"), FogRevealRT);

		FSlateBrush MapBrush = MapImage->GetBrush();
		MapBrush.SetResourceObject(HologramMapMID);
		MapImage->SetBrush(MapBrush);
		MapImage->SetColorAndOpacity(FLinearColor::White);
		// UE_LOG(LogTemp, Log, TEXT("[MapItem] 홀로그램 지도 표시 MID에 CaptureRT를 연결했습니다."));
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("[MapItem] WBP_Map에서 Img_Map 이미지를 찾지 못했습니다."));
	}

	HologramWidgetComponent->RequestRedraw();
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
		CloseMapWidget();
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

// [MAP-002] 레거시 장착 경로에서 소지자 상태와 메시 위치를 갱신
void AMapItem::OnEquipped_Implementation(AActor* Equipper)
{
	Super::OnEquipped_Implementation(Equipper);

	HoldingPlayer = Equipper;

	// [MAP-014] 인벤토리에서 꺼내 손에 붙일 때도 스케일 튐 복원
	RestoreMeshScale();
}

// [MAP-003] 레거시 장착 해제 경로에서 소지자 상태와 열린 지도를 정리
void AMapItem::OnUnequipped_Implementation(AActor* Equipper)
{
	HoldingPlayer = nullptr;

	// [MAP-015] 지도를 켠 채로 인벤토리에 수납하면 UI가 남으므로 강제로 닫는다.
	//   (OnUnequipped는 MulticastOnUnequipped 경유라 위젯이 있는 클라에서 실행된다)
	CloseMapWidget();

	Super::OnUnequipped_Implementation(Equipper);
}

// [MAP-011] 직접 PickUp 호출이 들어와도 고정형 지도 장치가 집히지 않도록 차단합니다.
void AMapItem::PickUp_Implementation(AActor* Picker)
{
	// 의도적으로 비워 둡니다. CarryingComponent를 우회한 직접 호출도 집기 상태를 만들지 않습니다.
}

// [MAP-012] 놓기: 소지자 해제 + 열려 있던 지도 UI 강제 닫기(전 클라)
void AMapItem::Drop_Implementation(FVector DropLocation, AActor* Dropper)
{
	HoldingPlayer = nullptr;
	// [MAP-015] Drop은 서버에서만 실행되므로, 원격 소지 클라의 위젯까지 닫으려면 Multicast로 전파한다.
	MulticastCloseMap();
	Super::Drop_Implementation(DropLocation, Dropper);
}

// [MAP-012] 던지기: 소지자 해제 + 열려 있던 지도 UI 강제 닫기(전 클라)
void AMapItem::Throw_Implementation(FVector ThrowVelocity, AActor* Thrower)
{
	HoldingPlayer = nullptr;
	// [MAP-015] Throw도 서버에서만 실행되므로 Multicast로 전 클라 위젯을 닫는다.
	MulticastCloseMap();
	Super::Throw_Implementation(ThrowVelocity, Thrower);
}

// [MAP-015] 열린 지도 오버레이 위젯을 로컬에서 닫는다 (위젯이 없는 머신에선 아무 일도 안 함).
void AMapItem::CloseMapWidget()
{
	if (MapWidgetInstance)
	{
		MapWidgetInstance->RemoveFromParent();
		MapWidgetInstance = nullptr;
	}
	bMapOpen = false;
}

// [MAP-015] 전 클라 위젯 닫기 (Drop/Throw는 서버 전용이라 소지 클라에 전파 필요).
void AMapItem::MulticastCloseMap_Implementation()
{
	CloseMapWidget();
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

// [MAP-014] 부착으로 튄 메시 스케일·위치를 손 소켓 기준으로 복원.
//   ① SnapToTargetNotIncludingScale 부착이 RelativeScale을 재계산해 크기가 튀는 것 복원.
//   ② CarryingComponent의 GetComponentsBoundingBox 위치보정이 캡처카메라(맵 상공 3000)까지
//      박스에 포함해 지도를 손 밖 공중으로 밀어내는 것을, RelativeLocation=0으로 되돌려 복원.
//   부착·보정이 같은 프레임 뒤에 일어날 수 있어, 즉시 + 다음 틱에 한 번 더 복원한다.
void AMapItem::RestoreMeshScale()
{
	// PickUp 시점엔 아직 부착 전이다(부착·위치보정은 CarryingComponent가 PickUp 이후에 함).
	// 따라서 즉시 복원은 부착돼 있을 때만 하고, 다음 틱 복원은 부착 여부와 무관하게 반드시 예약한다.
	if (IsAttachedToPlayer())
	{
		SetActorRelativeLocation(FVector::ZeroVector);
		if (MeshComponent)
		{
			MeshComponent->SetRelativeScale3D(InitialMeshScale);
		}
	}

	// 다음 틱: 이때는 부착·위치보정이 끝난 상태 → 손 소켓 원점으로 확실히 복원
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick([this]()
		{
			if (IsValid(this) && IsAttachedToPlayer())
			{
				SetActorRelativeLocation(FVector::ZeroVector);
				if (MeshComponent)
				{
					MeshComponent->SetRelativeScale3D(InitialMeshScale);
				}
			}
		});
	}
}

// 손(캐릭터 메시)에 붙어 있는지 확인 (바닥에 놓인 지도의 위치를 건드리지 않기 위함)
bool AMapItem::IsAttachedToPlayer() const
{
	return GetAttachParentActor() != nullptr;
}

// [MAP-007] 캡처 RT와 기존 WBP_Map 호환용 흰색 마스크 RT를 런타임 생성합니다.
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

	// 탐험 구역을 구분하지 않으므로 두 마스크 모두 전체 공개(흰색)로 시작한다.
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::White);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::White);
	}

	// 캡처 카메라 출력 대상을 이 인스턴스 RT로 연결
	if (CaptureCamera)
	{
		CaptureCamera->TextureTarget = CaptureRT;
	}
}

// [MAP-008] 레벨 이동/재시작 시 호환 마스크 초기화 및 지형 재캡처.
//   일반 레벨 이동(액터 재생성)이면 BeginPlay가 자동 초기화하므로 불필요.
//   Seamless Travel 등 아이템이 유지되는 경우 BP에서 이 함수를 호출한다.
void AMapItem::ResetMapForNewLevel()
{
	// 구역 분류를 사용하지 않으므로 새 레벨에서도 전체 공개 상태로 초기화한다.
	if (FogMaskRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogMaskRT, FLinearColor::White);
	}
	if (FogRevealRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FogRevealRT, FLinearColor::White);
	}

	// 새 레벨 경계 재인식
	ResolveMapBoundsFromVolume();

	// 지형을 다시 1회 캡처하도록 예약 (새 레벨 지형 로드 대기)
	bMapCaptured = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CaptureDelayTimerHandle, this, &AMapItem::CaptureMapOnce,
			0.5f, false);
	}
}
