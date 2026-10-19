// Copyright Epic Games, Inc. All Rights Reserved.


#include "TeamProject_MOUPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TeamProject_MOUGameMode.h"
#include "Subsystems/WarehouseDataSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Blueprint/UserWidget.h"
#include "TeamProject_MOU.h"
#include "Item/ItemSpawnRow.h"
#include "Item/TerminalShopWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Base/ProjectGameStateBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Engine/DataTable.h"
#include "Widgets/Input/SVirtualJoystick.h"

#include "Engine/GameInstance.h"
#include "Server/ServerSubsystem.h"

// 음성 RPC 창구. 컨트롤러는 음성 시스템의 내부를 몰라도 되지만,
// "모든 컨트롤러가 음성 창구를 하나씩 갖는다" 는 것은 컨트롤러의 책임이다
#include "Voice/VoiceComponent.h"

// 마이크/무전기 상태 표시. 로그인 위젯과 같은 이유로 여기서만 의존한다 -
// 위젯은 누가 자기를 띄우는지 몰라야 하고, 띄우는 정책은 컨트롤러 몫이다.
#include "Voice/RadioStatusWidget.h"
#include "Voice/VoiceStatusWidget.h"
#include "Player/MainCharacter.h"
#include "UI/MOU_CharacterStatusHUD.h"
#include "UI/SpectatorOverlayWidget.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Player/SpectatorCameraActor.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetTree.h"
#include "UI/InGameMenuWidget.h"
#include "UI/MOU_GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/CameraComponent.h"

// [LOBBYLOAD-007] 소유 클라이언트가 최초 로비 플레이 준비 완료를 서버에 보고합니다.
void ATeamProject_MOUPlayerController::ServerReportLobbyEntryReady_Implementation(int64 UserId)
{
	if (ATeamProject_MOUGameMode* Mode = GetWorld()->GetAuthGameMode<ATeamProject_MOUGameMode>())
		Mode->ReportLobbyEntryReady(this, UserId);
}

// [LEVELREADY-001] 이동 전후 동일한 참여자 키를 서버에서 구한다.
FString ATeamProject_MOUPlayerController::GetLevelReadyParticipantKey() const
{
	if (!PlaySessionId.IsEmpty()) return TEXT("Session:") + PlaySessionId;
	if (IsLocalController() && HasAuthority()) return TEXT("Host");
	return PlayerState ? TEXT("Player:") + PlayerState->GetPlayerName() : FString();
}

// [LEVELREADY-002] 서버가 현재 맵의 준비 확인을 소유 클라이언트에 요청한다.
void ATeamProject_MOUPlayerController::ClientBeginLevelReadyWait_Implementation()
{
	bLevelReadyReportPending = true;
}

// [LEVELREADY-003] 소유 클라이언트의 준비 완료를 서버에 보고한다.
void ATeamProject_MOUPlayerController::ServerReportLevelReady_Implementation()
{
	if (ATeamProject_MOUGameMode* Mode = GetWorld()->GetAuthGameMode<ATeamProject_MOUGameMode>())
		Mode->ReportLevelReady(this);
}

// [LEVELREADY-004] 맵과 소유 캐릭터의 필수 복제가 준비되면 보고한다.
void ATeamProject_MOUPlayerController::UpdateLevelReadyWait()
{
	if (!IsLocalController() || !bLevelReadyReportPending) return;
	const UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	const AProjectGameStateBase* State = GetWorld()->GetGameState<AProjectGameStateBase>();
	const AMainCharacter* ReadyPawn = Cast<AMainCharacter>(GetPawn());
	const UAbilitySystemComponent* ASC = ReadyPawn ? ReadyPawn->GetAbilitySystemComponent() : nullptr;
	if (!Instance || !Instance->MapLoaded || GetWorld()->IsInSeamlessTravel()
		|| !State || !State->HasLobbyEntryStorage() || !PlayerState || !ReadyPawn
		|| !ReadyPawn->HasActorBegunPlay() || !ReadyPawn->IsLocallyControlled()
		|| ReadyPawn->GetPlayerState() != PlayerState || !ASC || ASC->GetAvatarActor() != ReadyPawn) return;
	bLevelReadyReportPending = false;
	ServerReportLevelReady();
}

// [LOBBYLOAD-010] 소유 Pawn과 필수 복제 데이터가 준비되었는지 검사합니다.
bool ATeamProject_MOUPlayerController::IsLobbyEntryLocallyReady_Implementation() const
{
	const UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	const AProjectGameStateBase* State = GetWorld()->GetGameState<AProjectGameStateBase>();
	const AMainCharacter* ReadyCharacter = Cast<AMainCharacter>(GetPawn());
	const UAbilitySystemComponent* ASC = ReadyCharacter ? ReadyCharacter->GetAbilitySystemComponent() : nullptr;
	return Instance && Instance->MapLoaded && State && State->HasLobbyEntryStorage()
		&& PlayerState && ReadyCharacter && ReadyCharacter->HasActorBegunPlay()
		&& ReadyCharacter->IsLocallyControlled() && ReadyCharacter->GetPlayerState() == PlayerState
		&& ASC && ASC->GetAvatarActor() == ReadyCharacter;
}

// [LOBBYLOAD-008] 전원 준비 상태를 확인하여 로딩 표시와 로컬 조작 잠금을 갱신합니다.
void ATeamProject_MOUPlayerController::UpdateLobbyEntryWait()
{
	UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	AProjectGameStateBase* State = GetWorld()->GetGameState<AProjectGameStateBase>();
	if (!Instance) return;
	const bool bWaiting = (State && State->LobbyEntryPhase == 1)
		|| (IsLocalController() && Instance->bLobbyEntryWaiting && (!State || State->LobbyEntryPhase != 2));
	if (bWaiting != bLobbyEntryInputLocked)
	{
		SetIgnoreMoveInput(bWaiting);
		SetIgnoreLookInput(bWaiting);
		bLobbyEntryInputLocked = bWaiting;
	}
	if (bWaiting)
	{
		if (AMainCharacter* ReadyCharacter = Cast<AMainCharacter>(GetPawn()))
			ReadyCharacter->GetCharacterMovement()->StopMovementImmediately();
	}
	if (!IsLocalController()) return;
	if (State && State->LobbyEntryPhase == 2 && Instance->bLobbyEntryWaiting)
	{
		Instance->FinishLobbyEntryWait();
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		return;
	}
	if (!bWaiting || !Instance->bLobbyEntryWaiting) return;
	Instance->ShowLobbyEntryLoading();
	if (State && State->LobbyEntryPhase == 1 && IsLobbyEntryLocallyReady()
		&& GetWorld()->GetTimeSeconds() >= NextLobbyEntryReadyReport)
	{
		NextLobbyEntryReadyReport = GetWorld()->GetTimeSeconds() + 1.0;
		if (UServerSubsystem* Server = Instance->GetSubsystem<UServerSubsystem>())
			ServerReportLobbyEntryReady(Server->GetLoginResult().UserId);
	}
}

// [LOBBYLOAD-009] 입장 대기 중 이동·점프·상호작용을 포함한 게임 입력을 차단합니다.
void ATeamProject_MOUPlayerController::BuildInputStack(TArray<UInputComponent*>& InputStack)
{
	Super::BuildInputStack(InputStack);
	const UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	if (bLobbyEntryInputLocked || (Instance && Instance->bLobbyEntryWaiting)) InputStack.Reset();
}

ATeamProject_MOUPlayerController::ATeamProject_MOUPlayerController()
{
	// ★ 생성자에서 만들어야 서버와 클라이언트가 같은 컴포넌트를 갖는다.
	//   이유는 헤더의 VoiceComponent 주석 참고.
	VoiceComponent = CreateDefaultSubobject<UVoiceComponent>(TEXT("MOUVoiceComponent"));
}

// [SETTLEMENT-000] 정산 UI의 확인 상태를 권한을 가진 GameMode에 전달합니다.
void ATeamProject_MOUPlayerController::ServerSetSettlementConfirmed_Implementation(bool bConfirmed)
{
	if (ATeamProject_MOUGameMode* GameMode = GetWorld()->GetAuthGameMode<ATeamProject_MOUGameMode>())
	{
		GameMode->SetSettlementConfirmation(this, bConfirmed);
	}
}

void ATeamProject_MOUPlayerController::ServerSaveWarehouseDelivery_Implementation(
	const TArray<FStoredItemData>& RequestedItems)
{
	UWarehouseDataSubsystem* Warehouse = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr;
	const bool bSucceeded = Warehouse && Warehouse->SavePendingDeliveryDataFromRequest(RequestedItems);
	ClientWarehouseDeliverySaveCompleted(bSucceeded);
}

void ATeamProject_MOUPlayerController::ClientWarehouseDeliverySaveCompleted_Implementation(bool bSucceeded)
{
	OnWarehouseDeliverySaveCompleted.Broadcast(bSucceeded);
}

void ATeamProject_MOUPlayerController::ServerAddWarehouseDeliveryItem_Implementation(
	TSubclassOf<AItemBase> ItemClass, int32 Quantity)
{
	UWarehouseDataSubsystem* Warehouse = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr;
	const bool bSucceeded = Warehouse && Warehouse->AddPendingDeliveryItem(ItemClass, Quantity);
	ClientWarehouseDeliveryAddCompleted(bSucceeded);
}

void ATeamProject_MOUPlayerController::ClientWarehouseDeliveryAddCompleted_Implementation(bool bSucceeded)
{
	OnWarehouseDeliveryAddCompleted.Broadcast(bSucceeded);
}

void ATeamProject_MOUPlayerController::ServerRemoveWarehouseDeliveryItem_Implementation(
	TSubclassOf<AItemBase> ItemClass, int32 Quantity)
{
	UWarehouseDataSubsystem* Warehouse = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>() : nullptr;
	const bool bSucceeded = Warehouse && Warehouse->RemovePendingDeliveryItem(ItemClass, Quantity);
	ClientWarehouseDeliveryRemoveCompleted(bSucceeded);
}

void ATeamProject_MOUPlayerController::ClientWarehouseDeliveryRemoveCompleted_Implementation(bool bSucceeded)
{
	OnWarehouseDeliveryRemoveCompleted.Broadcast(bSucceeded);
}

void ATeamProject_MOUPlayerController::ServerSpendGold_Implementation(int32 Amount)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AProjectGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState<AProjectGameStateBase>() : nullptr)
	{
		GS->SpendGold(Amount);
	}
}

// [TSHOP-015] 서버에서 상품과 결제를 검증한 뒤 창고 재고와 개별 상태를 추가하고 동기화한다.
void ATeamProject_MOUPlayerController::ServerRequestTerminalPurchase_Implementation(const TArray<FTerminalCartItem>& Items)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Items.IsEmpty())
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "EmptyCart", "장바구니가 비어 있습니다."));
		return;
	}

	UDataTable* Table = TerminalShopItemTable;
	if (!Table)
	{
		Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/04_JJO/DT_Item.DT_Item"));
	}

	if (!Table)
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "TableNotFound", "상품 데이터를 찾을 수 없습니다."));
		return;
	}

	int64 TotalPrice = 0;
	TArray<TPair<const FItemSpawnRow*, int32>> ValidatedRows;

	for (const FTerminalCartItem& CartItem : Items)
	{
		if (CartItem.Quantity <= 0)
		{
			ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "InvalidQuantity", "구매 수량이 올바르지 않습니다."));
			return;
		}

		const FItemSpawnRow* Row = Table->FindRow<FItemSpawnRow>(CartItem.RowName, TEXT("TerminalShopPurchase"));
		if (!Row)
		{
			ClientTerminalPurchaseCompleted(false, FText::Format(
				NSLOCTEXT("TerminalShop", "ItemNotFound", "존재하지 않는 상품이 포함되어 있습니다: {0}"),
				FText::FromName(CartItem.RowName)));
			return;
		}

		if (!Row->ItemClass || Row->Price < 0)
		{
			ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "InvalidProduct", "상품 데이터가 올바르지 않습니다."));
			return;
		}

		TotalPrice += static_cast<int64>(Row->Price) * CartItem.Quantity;
		if (TotalPrice > MAX_int32)
		{
			ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "PriceOverflow", "구매 금액 한도를 초과했습니다."));
			return;
		}
		ValidatedRows.Emplace(Row, CartItem.Quantity);
	}

	if (ValidatedRows.IsEmpty())
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "InvalidQuantity", "구매 수량이 올바르지 않습니다."));
		return;
	}

	AProjectGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState<AProjectGameStateBase>() : nullptr;
	if (!GS)
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "NoGameState", "게임 상태를 확인할 수 없습니다."));
		return;
	}

	const int32 PaymentAmount = static_cast<int32>(TotalPrice);
	if (!GS->CanAfford(PaymentAmount))
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "InsufficientGold", "골드가 부족합니다."));
		return;
	}

	UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	UWarehouseDataSubsystem* Warehouse = Instance
		? Instance->GetSubsystem<UWarehouseDataSubsystem>() : nullptr;
	if (!Warehouse)
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "NoWarehouse", "창고 데이터를 확인할 수 없습니다."));
		return;
	}

	// 결제 전에 변경할 데이터를 준비하여 검증 실패 시 기존 재고와 골드를 유지한다.
	TArray<FStoredItemData> UpdatedItems = Warehouse->GetStoredItemsCopy();
	TArray<FStoredItemInstanceData> UpdatedInstances = Warehouse->GetStoredItemInstancesCopy();
	for (const auto& Pair : ValidatedRows)
	{
		const FItemSpawnRow* Row = Pair.Key;
		const int32 Quantity = Pair.Value;
		FStoredItemData* Existing = UpdatedItems.FindByPredicate(
			[Row](const FStoredItemData& Item) { return Item.ItemClass == Row->ItemClass; });
		if (Existing && Existing->Quantity > MAX_int32 - Quantity)
		{
			ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "StockOverflow", "창고 수량 한도를 초과했습니다."));
			return;
		}
		if (Existing)
		{
			Existing->Quantity += Quantity;
		}
		else
		{
			FStoredItemData Added;
			Added.ItemClass = Row->ItemClass;
			Added.Quantity = Quantity;
			UpdatedItems.Add(Added);
		}

		for (int32 Index = 0; Index < Quantity; ++Index)
		{
			FStoredItemInstanceData Added;
			Added.ItemClass = Row->ItemClass;
			Added.CurrentUseCount = Row->MaxUseCount;
			Added.CurrentDurability = Row->MaxDurability;
			UpdatedInstances.Add(MoveTemp(Added));
		}
	}

	if (!GS->SpendGold(PaymentAmount))
	{
		ClientTerminalPurchaseCompleted(false, NSLOCTEXT("TerminalShop", "SpendFailed", "골드 차감에 실패했습니다."));
		return;
	}

	// 요약과 개별 상태를 함께 반영한 뒤 한 번만 알림을 보내 서버와 클라이언트를 갱신한다.
	Instance->SavedStoredItems = MoveTemp(UpdatedItems);
	Instance->SavedStoredItemInstances = MoveTemp(UpdatedInstances);
	Instance->bWarehouseInitialized = true;
	Warehouse->NotifyStoredWarehouseChanged();

	ClientTerminalPurchaseCompleted(true, NSLOCTEXT("TerminalShop", "PurchaseSuccess", "구매가 완료되었습니다."));
}

void ATeamProject_MOUPlayerController::ClientTerminalPurchaseCompleted_Implementation(bool bSucceeded, const FText& ErrorMessage)
{
	OnTerminalPurchaseCompleted.Broadcast(bSucceeded, ErrorMessage);
}

// [BGMAUDIO-003] 로컬 플레이어 초기화 시 오디오 설정과 기존 조작 설정을 적용한다.
void ATeamProject_MOUPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogTeamProject_MOU, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	if (IsLocalPlayerController())
	{
		ApplyUserSettingsToPlayer();

		if (UMOU_GameUserSettings* Settings = UMOU_GameUserSettings::GetMOUGameUserSettings())
		{
			Settings->ApplyAudioSettings(this);
			Settings->OnControlSettingsChanged.AddUObject(this, &ATeamProject_MOUPlayerController::ApplyUserSettingsToPlayer);
		}
	}
}

void ATeamProject_MOUPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocalPlayerController())
	{
		if (UMOU_GameUserSettings* Settings = UMOU_GameUserSettings::GetMOUGameUserSettings())
		{
			Settings->OnControlSettingsChanged.RemoveAll(this);
		}
	}

	GetWorldTimerManager().ClearTimer(SpectatorTransitionTimerHandle);

	bIsDeathSequenceActive = false;

	if (bIsSpectating)
	{
		StopSpectating();
	}

	HideTurnOffDisplay();
	HideSpectatorOverlay();

	Super::EndPlay(EndPlayReason);
}

void ATeamProject_MOUPlayerController::PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel)
{
	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);

	if (IsLocalPlayerController())
	{
		TArray<UUserWidget*> FoundWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, FoundWidgets, UTerminalShopWidget::StaticClass(), false);
		for (UUserWidget* Widget : FoundWidgets)
		{
			if (UTerminalShopWidget* ShopWidget = Cast<UTerminalShopWidget>(Widget))
			{
				ShopWidget->ForceCloseShopImmediately();
			}
		}

		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void ATeamProject_MOUPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			if (IA_SpectateNext)
			{
				EnhancedInputComponent->BindAction(IA_SpectateNext, ETriggerEvent::Started, this, &ATeamProject_MOUPlayerController::SpectateNextPlayer);
			}
			if (IA_SpectatePrev)
			{
				EnhancedInputComponent->BindAction(IA_SpectatePrev, ETriggerEvent::Started, this, &ATeamProject_MOUPlayerController::SpectatePrevPlayer);
			}
			if (IA_SpectateLook)
			{
				EnhancedInputComponent->BindAction(IA_SpectateLook, ETriggerEvent::Triggered, this, &ATeamProject_MOUPlayerController::OnSpectatorLook);
			}
			if (IA_SpectateZoom)
			{
				EnhancedInputComponent->BindAction(IA_SpectateZoom, ETriggerEvent::Triggered, this, &ATeamProject_MOUPlayerController::OnSpectatorZoom);
			}
			if (IA_Menu)
			{
				EnhancedInputComponent->BindAction(IA_Menu, ETriggerEvent::Started, this, &ATeamProject_MOUPlayerController::ToggleInGameMenu);
			}
		}

		if (InputComponent)
		{
			InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ATeamProject_MOUPlayerController::OnSpectatorMouseWheel);
			InputComponent->BindAxisKey(EKeys::MouseX, this, &ATeamProject_MOUPlayerController::OnSpectatorTurn);
			InputComponent->BindAxisKey(EKeys::MouseY, this, &ATeamProject_MOUPlayerController::OnSpectatorLookUp);

			// ESC 키를 누르면 인게임 메뉴 토글 (메뉴가 닫혀있으면 열고, 열려있으면 닫음)
			InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATeamProject_MOUPlayerController::ToggleInGameMenu);
		}
	}
}

// 차량 운전 모드로 전환: 캐릭터 IMC를 모두 제거하고 차량 IMC 하나만 남긴다.
// (캐릭터 IMC가 남아 있으면 W/A/S/D 같은 공용 키를 먼저 소비해 차량 액션까지 도달하지 못한다.)
void ATeamProject_MOUPlayerController::SwitchToVehicleInput(UInputMappingContext* DrivingContext)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	// 캐릭터용 IMC 전부 제거 (SetupInputComponent 에서 추가한 것과 동일 목록).
	for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->RemoveMappingContext(CurrentContext);
		}
	}
	for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->RemoveMappingContext(CurrentContext);
		}
	}

	// 차량 IMC 추가.
	if (DrivingContext)
	{
		Subsystem->AddMappingContext(DrivingContext, 0);
		ActiveVehicleContext = DrivingContext;
	}
}

// 차량 운전 모드 해제: 차량 IMC를 제거하고 캐릭터 IMC를 원래대로 복원한다.
void ATeamProject_MOUPlayerController::RestoreCharacterInput()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	// 차량 IMC 제거.
	if (ActiveVehicleContext)
	{
		Subsystem->RemoveMappingContext(ActiveVehicleContext);
		ActiveVehicleContext = nullptr;
	}

	// 캐릭터용 IMC 복원 (SetupInputComponent 와 동일 규칙).
	for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
	{
		if (CurrentContext)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}
	if (!ShouldUseTouchControls())
	{
		for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
}

bool ATeamProject_MOUPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

// [LATEJOIN-011] 사망·합류 관전을 동기화하고 대상 사망·퇴장 시 생존자를 다시 찾습니다.
void ATeamProject_MOUPlayerController::PlayerTick(float DeltaTime)
{
	UpdateLevelReadyWait();
	UpdateLobbyEntryWait();
	Super::PlayerTick(DeltaTime);
    const AMainCharacter* OwnCharacter = Cast<AMainCharacter>(GetPawn());
    const bool bDeadPlayer = IsValid(OwnCharacter) && OwnCharacter->bIsDead;
    // 사망 연출 중 자기 Pawn을 보고 있을 때는 관전 시작 요청을 기다립니다.
    if (HasAuthority() && (bWaitForSafeLobby || (bDeadPlayer && GetViewTarget() != OwnCharacter)))
    {
        AMainCharacter* Target = Cast<AMainCharacter>(GetViewTarget());
        if (!IsValid(Target) || Target->IsActorBeingDestroyed() || Target->bIsDead || !Target->IsPlayerControlled())
            ServerCycleLateJoinTarget(1);
    }
    if (IsLocalPlayerController())
    {
        if (bWaitForSafeLobby && !bIsSpectating) StartSpectating();
        if (!bWaitForSafeLobby && bIsSpectating && IsValid(OwnCharacter) && !OwnCharacter->bIsDead)
            StopSpectating();
        if (bIsSpectating)
        {
            AMainCharacter* Target = Cast<AMainCharacter>(GetViewTarget());
            if (IsValid(Target) && !Target->IsActorBeingDestroyed() && !Target->bIsDead)
            {
                if (CurrentSpectateTarget.Get() != Target)
                    SetSpectateTarget(Target, 0.f);

                ShowSpectatorOverlay();
            }
            else
            {
                if (CurrentSpectateTarget.IsValid())
                    CurrentSpectateTarget->EnableSpectatorCamera(false);
                CurrentSpectateTarget = nullptr;
                HideSpectatorOverlay();

                const double Now = GetWorld()->GetTimeSeconds();
                if (Now >= NextSpectateTargetRetryTime)
                {
                    NextSpectateTargetRetryTime = Now + 0.5;
                    ServerCycleLateJoinTarget(0);
                }
            }
        }
    }

	if (!IsLocalPlayerController())
	{
		return;
	}

	AActor* CurrentVT = GetViewTarget();
	if (CurrentVT != LastViewTarget.Get())
	{
		LastViewTarget = CurrentVT;
		APawn* MyPawn = GetPawn();
		const bool bIsSelf = (CurrentVT == MyPawn && MyPawn != nullptr);

		OnViewTargetActorChanged.Broadcast(CurrentVT, bIsSelf);

		if (!bIsSpectating)
		{
			if (!bIsSelf)
			{
				SetInGameUIHidden(true);
			}
			else
			{
				AMainCharacter* MainChar = Cast<AMainCharacter>(MyPawn);
				if (MainChar && !MainChar->bIsDead)
				{
					SetInGameUIHidden(false);
					if (StatusHUDWidget)
					{
						StatusHUDWidget->BindToCharacter(MainChar);
					}
				}
			}
		}
	}

	if (bIsSpectating)
	{
		if (CurrentSpectateTarget.IsValid() && IsLocalPlayerController())
		{
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			GetInputMouseDelta(MouseX, MouseY);
			if (!FMath::IsNearlyZero(MouseX) || !FMath::IsNearlyZero(MouseY))
			{
				CurrentSpectateTarget->AddSpectatorOrbit(MouseY, MouseX);
			}

			float WheelDelta = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
			if (!FMath::IsNearlyZero(WheelDelta))
			{
				CurrentSpectateTarget->AddSpectatorZoom(WheelDelta);
			}
			else if (IsInputKeyDown(EKeys::MouseScrollUp))
			{
				CurrentSpectateTarget->AddSpectatorZoom(1.0f);
			}
			else if (IsInputKeyDown(EKeys::MouseScrollDown))
			{
				CurrentSpectateTarget->AddSpectatorZoom(-1.0f);
			}
		}
	}
}

// [SPECTATE-000] 자신과 사망 또는 퇴장하거나 제거 중인 캐릭터를 제외한 생존자를 조회합니다.
TArray<AMainCharacter*> ATeamProject_MOUPlayerController::GetAliveTeammates() const
{
	TArray<AMainCharacter*> AliveList;
	UWorld* World = GetWorld();
	if (!World)
	{
		return AliveList;
	}

	APawn* MyPawn = GetPawn();
	for (TActorIterator<AMainCharacter> It(World); It; ++It)
	{
		AMainCharacter* Char = *It;
		if (!IsValid(Char) || Char->IsActorBeingDestroyed() || Char == MyPawn)
		{
			continue;
		}
		if (!Char->IsPlayerControlled())
		{
			continue;
		}
		if (Char->bIsDead)
		{
			continue;
		}

		AliveList.Add(Char);
	}
	return AliveList;
}

// [LATEJOIN-009] 관전 중 서버의 전체 생존자 목록에서 다음 대상을 선택합니다.
void ATeamProject_MOUPlayerController::SpectateNextPlayer()
{
    if (!IsLocalPlayerController() || !bIsSpectating)
    {
        return;
    }
    ServerCycleLateJoinTarget(1);
}

// [LATEJOIN-010] 관전 중 서버의 전체 생존자 목록에서 이전 대상을 선택합니다.
void ATeamProject_MOUPlayerController::SpectatePrevPlayer()
{
    if (!IsLocalPlayerController() || !bIsSpectating)
    {
        return;
    }
    ServerCycleLateJoinTarget(-1);
}

void ATeamProject_MOUPlayerController::SetSpectateTarget(AMainCharacter* NewTarget, float BlendTime)
{
	if (!NewTarget || NewTarget->bIsDead)
	{
		return;
	}

	if (CurrentSpectateTarget.IsValid() && CurrentSpectateTarget.Get() != NewTarget)
	{
		CurrentSpectateTarget->EnableSpectatorCamera(false);
	}

	CurrentSpectateTarget = NewTarget;
	NewTarget->EnableSpectatorCamera(true);

	SetViewTargetWithBlend(NewTarget, BlendTime, EViewTargetBlendFunction::VTBlend_EaseInOut, 2.0f, true);

	if (StatusHUDWidget)
	{
		StatusHUDWidget->BindToCharacter(NewTarget);
		StatusHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	UpdateSpectatorOverlay();
}

// [LATEJOIN-008] 사망 또는 도중 합류 관전을 시작하고 서버의 대상 선택과 복제를 기다립니다.
void ATeamProject_MOUPlayerController::StartSpectating()
{
	if (!IsLocalPlayerController() || bIsSpectating)
	{
		return;
	}

	AMainCharacter* MyChar = Cast<AMainCharacter>(GetPawn());
	if (!bWaitForSafeLobby && (!MyChar || !MyChar->bIsDead))
	{
		return;
	}

	bIsSpectating = true;

	bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (SpectatorMappingContext)
		{
			Subsystem->AddMappingContext(SpectatorMappingContext, SpectatorMappingPriority);
		}
	}

    NextSpectateTargetRetryTime = 0.0;
    HideSpectatorOverlay();
    ServerCycleLateJoinTarget(0);
}

void ATeamProject_MOUPlayerController::StopSpectating()
{
	if (CurrentSpectateTarget.IsValid())
	{
		CurrentSpectateTarget->EnableSpectatorCamera(false);
	}

	bIsSpectating = false;
	CurrentSpectateTarget = nullptr;
	CurrentSpectateIndex = -1;

	if (SpectatorCameraActor)
	{
		SpectatorCameraActor->Destroy();
		SpectatorCameraActor = nullptr;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (SpectatorMappingContext)
		{
			Subsystem->RemoveMappingContext(SpectatorMappingContext);
		}
	}

	HideSpectatorOverlay();

	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (StatusHUDWidget)
	{
		StatusHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ATeamProject_MOUPlayerController::CheckSpectateTargetAlive()
{
	if (!CurrentSpectateTarget.IsValid() || CurrentSpectateTarget->bIsDead)
	{
		SpectateNextPlayer();
	}
}

void ATeamProject_MOUPlayerController::StartDeathSpectatorSequence()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	bIsDeathSequenceActive = true;
	SetInGameUIHidden(true);
	ShowTurnOffDisplay();

	GetWorldTimerManager().ClearTimer(SpectatorTransitionTimerHandle);
	if (DeathSpectatorDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(SpectatorTransitionTimerHandle, this,
			&ATeamProject_MOUPlayerController::OnTurnOffDisplayFinished,
			DeathSpectatorDelay, false);
	}
}

void ATeamProject_MOUPlayerController::OnTurnOffDisplayFinished()
{
	GetWorldTimerManager().ClearTimer(SpectatorTransitionTimerHandle);
	HideTurnOffDisplay();

	if (!bIsDeathSequenceActive)
	{
		return;
	}
	bIsDeathSequenceActive = false;

	AMainCharacter* MyChar = Cast<AMainCharacter>(GetPawn());
	if (MyChar && MyChar->bIsDead)
	{
		StartSpectating();
	}
}

void ATeamProject_MOUPlayerController::ShowTurnOffDisplay()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!TurnOffDisplayWidget && TurnOffDisplayWidgetClass)
	{
		TurnOffDisplayWidget = CreateWidget<UUserWidget>(this, TurnOffDisplayWidgetClass);
	}

	if (TurnOffDisplayWidget && !TurnOffDisplayWidget->IsInViewport())
	{
		TurnOffDisplayWidget->AddToViewport(1000);
	}
}

void ATeamProject_MOUPlayerController::HideTurnOffDisplay()
{
	if (TurnOffDisplayWidget && TurnOffDisplayWidget->IsInViewport())
	{
		TurnOffDisplayWidget->RemoveFromParent();
	}
}

void ATeamProject_MOUPlayerController::ShowSpectatorOverlay()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (!SpectatorOverlayWidget && SpectatorOverlayWidgetClass)
	{
		SpectatorOverlayWidget = CreateWidget<USpectatorOverlayWidget>(this, SpectatorOverlayWidgetClass);
	}

	if (SpectatorOverlayWidget && !SpectatorOverlayWidget->IsInViewport())
	{
		SpectatorOverlayWidget->AddToViewport(500);
	}

	UpdateSpectatorOverlay();
}

void ATeamProject_MOUPlayerController::HideSpectatorOverlay()
{
	if (SpectatorOverlayWidget && SpectatorOverlayWidget->IsInViewport())
	{
		SpectatorOverlayWidget->RemoveFromParent();
	}
}

void ATeamProject_MOUPlayerController::UpdateSpectatorOverlay()
{
	if (SpectatorOverlayWidget)
	{
		TArray<AMainCharacter*> AliveList = GetAliveTeammates();
		SpectatorOverlayWidget->SetSpectatorInfo(CurrentSpectateTarget.Get(), AliveList.Num());
	}
}

void ATeamProject_MOUPlayerController::RegisterStatusHUDWidget(UMOU_CharacterStatusHUD* InStatusHUD)
{
	StatusHUDWidget = InStatusHUD;
	if (InStatusHUD)
	{
		if (AMainCharacter* MainChar = Cast<AMainCharacter>(GetPawn()))
		{
			InStatusHUD->BindToCharacter(MainChar);
		}
	}
}

// [PCUI-002] HUD 내부의 상태·마이크·무전기 위젯을 찾아 컨트롤러 참조에 연결한다.
void ATeamProject_MOUPlayerController::RegisterPlayerHUDWidget(UUserWidget* InPlayerHUD)
{
	PlayerHUDWidget = InPlayerHUD;
	VoiceStatusWidget = nullptr;
	RadioStatusWidget = nullptr;
	if (InPlayerHUD)
	{
		if (InPlayerHUD->WidgetTree)
		{
			InPlayerHUD->WidgetTree->ForEachWidget([this](UWidget* Widget)
			{
				if (UVoiceStatusWidget* FoundVoice = Cast<UVoiceStatusWidget>(Widget))
				{
					VoiceStatusWidget = FoundVoice;
				}
				if (URadioStatusWidget* FoundRadio = Cast<URadioStatusWidget>(Widget))
				{
					RadioStatusWidget = FoundRadio;
				}
				if (UMOU_CharacterStatusHUD* FoundStatusHUD = Cast<UMOU_CharacterStatusHUD>(Widget))
				{
					RegisterStatusHUDWidget(FoundStatusHUD);
				}
			});
		}
	}
}

void ATeamProject_MOUPlayerController::SetInGameUIHidden(bool bInHidden)
{
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(bInHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (StatusHUDWidget)
	{
		StatusHUDWidget->SetVisibility(bInHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	OnInGameUIVisibilityChanged(!bInHidden);
}

void ATeamProject_MOUPlayerController::OnSpectatorLook(const FInputActionValue& Value)
{
	if (!bIsSpectating || !CurrentSpectateTarget.IsValid())
	{
		return;
	}

	FVector2D LookAxisVector = Value.Get<FVector2D>();
	CurrentSpectateTarget->AddSpectatorOrbit(LookAxisVector.Y, LookAxisVector.X);
}

void ATeamProject_MOUPlayerController::OnSpectatorZoom(const FInputActionValue& Value)
{
	if (!bIsSpectating || !CurrentSpectateTarget.IsValid())
	{
		return;
	}

	float ZoomDelta = Value.Get<float>();
	CurrentSpectateTarget->AddSpectatorZoom(ZoomDelta);
}

void ATeamProject_MOUPlayerController::OnSpectatorMouseWheel(float Val)
{
	if (!bIsSpectating || !CurrentSpectateTarget.IsValid() || FMath::IsNearlyZero(Val))
	{
		return;
	}

	CurrentSpectateTarget->AddSpectatorZoom(Val);
}

void ATeamProject_MOUPlayerController::OnSpectatorTurn(float Val)
{
	if (!bIsSpectating || !CurrentSpectateTarget.IsValid() || FMath::IsNearlyZero(Val))
	{
		return;
	}

	CurrentSpectateTarget->AddSpectatorOrbit(0.0f, Val);
}

void ATeamProject_MOUPlayerController::OnSpectatorLookUp(float Val)
{
	if (!bIsSpectating || !CurrentSpectateTarget.IsValid() || FMath::IsNearlyZero(Val))
	{
		return;
	}

	CurrentSpectateTarget->AddSpectatorOrbit(Val, 0.0f);
}

// ---------------------------------------------------------------------------
// 인게임 메뉴 (ESC 일시정지) 및 환경설정 연동
// ---------------------------------------------------------------------------

void ATeamProject_MOUPlayerController::ToggleInGameMenu()
{
	if (bIsInGameMenuOpen)
	{
		if (InGameMenuWidget)
		{
			InGameMenuWidget->HandleBackOrEscape();
		}
		else
		{
			CloseInGameMenu();
		}
	}
	else
	{
		OpenInGameMenu();
	}
}

void ATeamProject_MOUPlayerController::OpenInGameMenu()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	bIsInGameMenuOpen = true;

	if (!InGameMenuWidget)
	{
		UClass* WidgetClass = InGameMenuWidgetClass
			? InGameMenuWidgetClass.Get()
			: UInGameMenuWidget::StaticClass();

		InGameMenuWidget = CreateWidget<UInGameMenuWidget>(this, WidgetClass);
	}

	if (InGameMenuWidget && !InGameMenuWidget->IsInViewport())
	{
		InGameMenuWidget->AddToViewport(100);
	}

	FInputModeGameAndUI InputMode;
	if (InGameMenuWidget)
	{
		InputMode.SetWidgetToFocus(InGameMenuWidget->TakeWidget());
	}
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	bShowMouseCursor = true;
	SetIgnoreLookInput(true);
}

void ATeamProject_MOUPlayerController::CloseInGameMenu()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	bIsInGameMenuOpen = false;

	if (InGameMenuWidget && InGameMenuWidget->IsInViewport())
	{
		InGameMenuWidget->RemoveFromParent();
	}
	InGameMenuWidget = nullptr;

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);

	bShowMouseCursor = false;
	SetIgnoreLookInput(false);
}

// [LOBBYRETURN-001] 로그인 연결은 유지하고 방을 나간 뒤 메인로비 레벨로 이동한다.
void ATeamProject_MOUPlayerController::ReturnToLobby()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UServerSubsystem* Server = GI->GetSubsystem<UServerSubsystem>())
		{
			Server->LeaveRoom();
		}
	}

	CloseInGameMenu();
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/02_JSY/MainLobby/MainLobby")));
}

// [REJOIN-006] 서버 연결을 정리하여 퇴장 처리를 유도한 뒤 게임을 종료한다.
void ATeamProject_MOUPlayerController::QuitToDesktop()
{
    if (!IsLocalPlayerController()) return;
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UServerSubsystem* Server = GI->GetSubsystem<UServerSubsystem>())
            Server->Disconnect();
    }
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ATeamProject_MOUPlayerController::ApplyUserSettingsToPlayer()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UMOU_GameUserSettings* Settings = UMOU_GameUserSettings::GetMOUGameUserSettings();
	if (!Settings)
	{
		return;
	}

	// 1. FOV 적용
	const float TargetFOV = Settings->GetFieldOfView();
	if (PlayerCameraManager)
	{
		PlayerCameraManager->SetFOV(TargetFOV);
	}

	if (APawn* MyPawn = GetPawn())
	{
		if (UCameraComponent* Cam = MyPawn->FindComponentByClass<UCameraComponent>())
		{
			Cam->SetFieldOfView(TargetFOV);
		}
	}

	// 2. Enhanced Input 커스텀 키 리매핑 적용
	TArray<UInputMappingContext*> AllContexts = DefaultMappingContexts;
	for (UInputMappingContext* Ctx : MobileExcludedMappingContexts)
	{
		if (Ctx)
		{
			AllContexts.AddUnique(Ctx);
		}
	}
	if (ActiveVehicleContext)
	{
		AllContexts.AddUnique(ActiveVehicleContext);
	}

	// 이동(WASD) 매핑 방향 판별 헬퍼 람다
	auto GetMoveDirectionName = [](const FEnhancedActionKeyMapping& Mapping) -> FName
	{
		bool bHasSwizzle = false;
		bool bHasNegate = false;

		for (const TObjectPtr<UInputModifier>& Mod : Mapping.Modifiers)
		{
			if (Mod)
			{
				if (Mod->IsA<UInputModifierSwizzleAxis>())
				{
					bHasSwizzle = true;
				}
				else if (Mod->IsA<UInputModifierNegate>())
				{
					bHasNegate = true;
				}
			}
		}

		if (bHasSwizzle)
		{
			return bHasNegate ? FName("Move_Backward") : FName("Move_Forward");
		}
		else
		{
			return bHasNegate ? FName("Move_Left") : FName("Move_Right");
		}
	};

	// 최초 1회 기본 키 캐시
	if (DefaultKeyBindingsCache.Num() == 0)
	{
		for (UInputMappingContext* Context : AllContexts)
		{
			if (!Context)
			{
				continue;
			}

			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				if (const UInputAction* Action = Mapping.Action)
				{
					const FName ActionName = Action->GetFName();
					if (ActionName == FName("IA_Move") || ActionName == FName("Move"))
					{
						const FName DirectionName = GetMoveDirectionName(Mapping);
						if (!DefaultKeyBindingsCache.Contains(DirectionName))
						{
							DefaultKeyBindingsCache.Add(DirectionName, Mapping.Key);
						}
					}
					else if (!DefaultKeyBindingsCache.Contains(ActionName))
					{
						DefaultKeyBindingsCache.Add(ActionName, Mapping.Key);
					}
				}
			}
		}

		// 프로젝트 표준 기본키 우선 반영 (사용자 지정 기본값)
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_Interact")) = EKeys::F;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_Grab_Drop")) = EKeys::E;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_Throw")) = EKeys::Q;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_Slap")) = EKeys::T;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_Light")) = EKeys::Four;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_LightColor")) = EKeys::Five;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_EmoteToggle")) = EKeys::Tab;
		DefaultKeyBindingsCache.FindOrAdd(FName("IA_ViewEcnomoy")) = EKeys::V;
	}

	const TMap<FName, FKey>& CustomKeys = Settings->GetAllCustomKeyBindings();
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());

	if (Subsystem)
	{
		bool bAnyMappingChanged = false;

		for (UInputMappingContext* Context : AllContexts)
		{
			if (!Context)
			{
				continue;
			}

			for (int32 i = 0; i < Context->GetMappings().Num(); ++i)
			{
				FEnhancedActionKeyMapping& Mapping = Context->GetMapping(i);
				if (const UInputAction* Action = Mapping.Action)
				{
					const FName ActionName = Action->GetFName();
					FKey TargetKey;

					if (ActionName == FName("IA_Move") || ActionName == FName("Move"))
					{
						const FName DirectionName = GetMoveDirectionName(Mapping);
						if (const FKey* FoundCustom = CustomKeys.Find(DirectionName))
						{
							TargetKey = *FoundCustom;
						}
						else if (const FKey* FoundDefault = DefaultKeyBindingsCache.Find(DirectionName))
						{
							TargetKey = *FoundDefault;
						}
					}
					else
					{
						// 일반 액션 키 적용 (커스텀 키 설정이 있으면 커스텀 키, 없으면 캐시된 기본 키 사용)
						if (const FKey* FoundCustom = CustomKeys.Find(ActionName))
						{
							TargetKey = *FoundCustom;
						}
						else if (const FKey* FoundDefault = DefaultKeyBindingsCache.Find(ActionName))
						{
							TargetKey = *FoundDefault;
						}
					}

					if (Mapping.Key != TargetKey)
					{
						Mapping.Key = TargetKey;
						bAnyMappingChanged = true;
					}
				}
			}
		}

		if (bAnyMappingChanged)
		{
			Subsystem->RequestRebuildControlMappings(FModifyContextOptions(), EInputMappingRebuildType::Rebuild);
		}
	}

	// 3. 무전기 및 보이스 상태 위젯 단축키 동기화
	if (RadioStatusWidget)
	{
		if (const FKey* FoundPower = CustomKeys.Find(FName("Radio_Power")))
		{
			RadioStatusWidget->PowerToggleKey = *FoundPower;
		}
		if (const FKey* FoundTransmit = CustomKeys.Find(FName("Radio_Transmit")))
		{
			RadioStatusWidget->TransmitKey = *FoundTransmit;
		}
	}
	if (VoiceStatusWidget)
	{
		if (const FKey* FoundMute = CustomKeys.Find(FName("Voice_Mute")))
		{
			VoiceStatusWidget->MuteToggleKey = *FoundMute;
		}
	}
}





// [LATEJOIN-004] 도중 합류의 관전 제한을 소유 클라이언트로 복제합니다.
void ATeamProject_MOUPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ATeamProject_MOUPlayerController, bWaitForSafeLobby);
}

// [LATEJOIN-007] 심리스 이동으로 컨트롤러가 교체되어도 합류 제한을 보존합니다.
void ATeamProject_MOUPlayerController::SeamlessTravelTo(APlayerController* NewPC)
{
    Super::SeamlessTravelTo(NewPC);
    if (ATeamProject_MOUPlayerController* Next = Cast<ATeamProject_MOUPlayerController>(NewPC))
    {
        Next->PlaySessionId = PlaySessionId;
        Next->bWaitForSafeLobby = bWaitForSafeLobby;
    }
}

// [LATEJOIN-014] 사망·합류 관전 요청에서 전체 생존자를 선택하고 대상이 없으면 대기합니다.
void ATeamProject_MOUPlayerController::ServerCycleLateJoinTarget_Implementation(int32 Direction)
{
    const AMainCharacter* OwnCharacter = Cast<AMainCharacter>(GetPawn());
    if (!bWaitForSafeLobby && (!IsValid(OwnCharacter) || !OwnCharacter->bIsDead))
        return;
    const TArray<AMainCharacter*> Alive = GetAliveTeammates();
    if (Alive.IsEmpty())
    {
        // 퇴장한 대상 참조를 해제하고 새 생존자가 생길 때까지 관전을 유지합니다.
        if (GetViewTarget() != this)
        {
            SetViewTarget(this);
            ClientSetViewTarget(this);
        }
        return;
    }
    const int32 Previous = Alive.IndexOfByKey(Cast<AMainCharacter>(GetViewTarget()));
    const int32 Index = Previous == INDEX_NONE ? 0
        : Direction == 0 ? Previous
        : (Previous + (Direction < 0 ? -1 : 1) + Alive.Num()) % Alive.Num();
    // 서버의 ViewTarget도 갱신해야 관전 위치를 기준으로 네트워크 관련성을 판단합니다.
    SetViewTarget(Alive[Index]);
    ClientSetViewTarget(Alive[Index]);
}
