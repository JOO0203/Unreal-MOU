// Copyright Epic Games, Inc. All Rights Reserved.

#include "TeamProject_MOUGameMode.h"
#include "TeamProject_MOUPlayerController.h"
#include "Server/ServerSubsystem.h"

#include "Base/ItemBase.h"
#include "Base/ProjectGameInstanceBase.h"
#include "Base/ProjectGameStateBase.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/RunState.h"
#include "Game/GameCycleState.h"
#include "Game/LevelTimerConfigDataAsset.h"
#include "Game/LevelTimerState.h"
#include "Game/LevelSettlementState.h"
#include "Delivery/DeliveryManager.h"
#include "Player/MainCharacter.h"
#include "Subsystems/WarehouseDataSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyEntryReadyRegressionTest, "MOU.LobbyEntry.ReadyBarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// [LOBBYLOAD-011] 늦은 접속, 중복 보고, 명단 외 보고와 전원 준비 해제를 검증합니다.
bool FLobbyEntryReadyRegressionTest::RunTest(const FString& Parameters)
{
	TGuardValue<bool> AllowActorEvents(GAllowActorScriptExecutionInEditor, true);
	UProjectGameInstanceBase* Instance = NewObject<UProjectGameInstanceBase>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	UClass* ModeClass = LoadClass<ATeamProject_MOUGameMode>(nullptr,
		TEXT("/Game/01_LDJ/GameSystem/BP/BP_Gamemode.BP_Gamemode_C"));
	if (!TestNotNull(TEXT("Production game mode"), ModeClass)) { Instance->Shutdown(); return false; }
	auto* Mode = World->SpawnActor<ATeamProject_MOUGameMode>(ModeClass);
	auto* State = World->SpawnActor<AProjectGameStateBase>();
	World->SetGameState(State);
	Mode->GameState = State;
	if (!TestNotNull(TEXT("Production game state"), State)) { Instance->Shutdown(); return false; }
	const FString Map = UGameplayStatics::GetCurrentLevelName(World, true);
	Mode->LobbyMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Test/") + Map + TEXT(".") + Map));
	Instance->bLobbyEntryWaiting = true;
	Instance->LobbyEntryExpectedMembers = { 101, 202 };
	State->LobbyEntryPhase = 1;
	auto* Host = World->SpawnActor<ATeamProject_MOUPlayerController>(Mode->PlayerControllerClass);
	auto* Guest = World->SpawnActor<ATeamProject_MOUPlayerController>(Mode->PlayerControllerClass);
	if (!Host->PlayerState) Host->PlayerState = World->SpawnActor<APlayerState>();
	if (!Guest->PlayerState) Guest->PlayerState = World->SpawnActor<APlayerState>();
	auto* HostPawn = World->SpawnActor<AMainCharacter>();
	auto* GuestPawn = World->SpawnActor<AMainCharacter>();
	Host->Possess(HostPawn);
	Mode->ReportLobbyEntryReady(Host, 999);
	TestEqual(TEXT("Unexpected member is ignored"), Mode->LobbyEntryReadyMembers.Num(), 0);
	Mode->ReportLobbyEntryReady(Host, 101);
	TestEqual(TEXT("Host alone cannot release pending guest"), State->LobbyEntryPhase, uint8(1));
	Mode->ReportLobbyEntryReady(Host, 202);
	TestEqual(TEXT("One controller cannot report as two members"), State->LobbyEntryPhase, uint8(1));
	Mode->ReportLobbyEntryReady(Guest, 202);
	TestEqual(TEXT("Guest without pawn cannot release barrier"), State->LobbyEntryPhase, uint8(1));
	Guest->Possess(GuestPawn);
	Mode->ReportLobbyEntryReady(Guest, 202);
	TestEqual(TEXT("All expected members release barrier"), State->LobbyEntryPhase, uint8(2));
	Instance->FinishLobbyEntryWait();
	TestFalse(TEXT("Completion clears initial-entry flag"), Instance->bLobbyEntryWaiting);
	TestTrue(TEXT("Completion clears expected roster"), Instance->LobbyEntryExpectedMembers.IsEmpty());
	Host->Destroy(); Guest->Destroy(); HostPawn->Destroy(); GuestPawn->Destroy(); Mode->Destroy();
	Instance->Shutdown();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelReadyRegressionTest, "MOU.LevelTimer.ReadyBarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// [LEVELREADY-008] 늦은 참여자와 중복 보고를 검사하고 전원 준비 후 한 번만 시작되는지 검증한다.
bool FLevelReadyRegressionTest::RunTest(const FString& Parameters)
{
	TGuardValue<bool> AllowActorEvents(GAllowActorScriptExecutionInEditor, true);
	UProjectGameInstanceBase* Instance = NewObject<UProjectGameInstanceBase>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	UClass* ModeClass = LoadClass<ATeamProject_MOUGameMode>(nullptr,
		TEXT("/Game/01_LDJ/GameSystem/BP/BP_Gamemode.BP_Gamemode_C"));
	if (!TestNotNull(TEXT("Production game mode"), ModeClass)) { Instance->Shutdown(); return false; }
	auto* Mode = World->SpawnActor<ATeamProject_MOUGameMode>(ModeClass);
	auto* Host = World->SpawnActor<ATeamProject_MOUPlayerController>(Mode->PlayerControllerClass);
	Host->PlaySessionId = TEXT("ready-host");
	Host->bWaitForSafeLobby = false;
	World->AddController(Host);
	if (!Host->PlayerState) Host->PlayerState = World->SpawnActor<APlayerState>();
	auto* HostPawn = World->SpawnActor<AMainCharacter>();
	Host->Possess(HostPawn);
	Mode->ExpectedLevelParticipants = { TEXT("Session:ready-host"), TEXT("Session:ready-guest") };
	Mode->bWaitingForLevelReady = true;
	Mode->ReportLevelReady(Host);
	TestEqual(TEXT("Unrequested report ignored"), Mode->ReadyLevelPlayers.Num(), 0);
	Mode->CheckLevelReadyWait();
	Mode->ReportLevelReady(Host);
	Mode->ReportLevelReady(Host);
	TestFalse(TEXT("Host cannot start before late guest arrives"), Mode->bLevelTimerStarted);
	TestEqual(TEXT("Duplicate reports counted once"), Mode->ReadyLevelPlayers.Num(), 1);
	auto* Guest = World->SpawnActor<ATeamProject_MOUPlayerController>(Mode->PlayerControllerClass);
	Guest->PlaySessionId = TEXT("ready-guest");
	Guest->bWaitForSafeLobby = false;
	World->AddController(Guest);
	if (!Guest->PlayerState) Guest->PlayerState = World->SpawnActor<APlayerState>();
	Mode->CheckLevelReadyWait();
	Mode->ReportLevelReady(Guest);
	TestFalse(TEXT("Guest without pawn cannot start timer"), Mode->bLevelTimerStarted);
	auto* GuestPawn = World->SpawnActor<AMainCharacter>();
	Guest->Possess(GuestPawn);
	Mode->ReportLevelReady(Guest);
	TestTrue(TEXT("All participants ready starts timer"), Mode->bLevelTimerStarted);
	TestFalse(TEXT("Waiting ends once"), Mode->bWaitingForLevelReady);
	Mode->ReportLevelReady(Guest);
	TestEqual(TEXT("Completion ignores repeat reports"), Mode->ReadyLevelPlayers.Num(), 2);
	Instance->CaptureLevelTravelParticipants(World);
	TestTrue(TEXT("Host retained across travel"), Instance->LevelTravelParticipants.Contains(TEXT("Session:ready-host")));
	TestTrue(TEXT("Guest retained across travel"), Instance->LevelTravelParticipants.Contains(TEXT("Session:ready-guest")));
	Host->Destroy(); Guest->Destroy(); HostPawn->Destroy(); GuestPawn->Destroy(); Mode->Destroy();
	Instance->Shutdown();
	return true;
}
#endif

ATeamProject_MOUGameMode::ATeamProject_MOUGameMode()
{
}

void ATeamProject_MOUGameMode::InitGameState()
{
	Super::InitGameState();
	if (const UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		if (Instance->bLobbyEntryWaiting && IsLobbyLevel())
			if (AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>())
				State->LobbyEntryPhase = 1;
	}
	RunState = GetWorld()->SpawnActor<ARunState>();
	GameCycleState = GetWorld()->SpawnActor<AGameCycleState>();
	LevelTimerState = GetWorld()->SpawnActor<ALevelTimerState>();
	LevelSettlementState = GetWorld()->SpawnActor<ALevelSettlementState>();
	DeliveryManager = GetWorld()->SpawnActor<ADeliveryManager>();
	if (AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>())
	{
		GameCycleState->InitializeEconomyTime(State->GetEconomyCurrentHalfDay());
	}
}

void ATeamProject_MOUGameMode::BeginPlay()
{
	Super::BeginPlay();
	BeginLevelReadyWait();
}

// [LEVELREADY-006] 이동 참여자 전원의 로컬 준비 보고를 기다린다.
void ATeamProject_MOUGameMode::BeginLevelReadyWait()
{
	float Duration = 0.0f;
	if (!LevelTimerConfig || !LevelTimerState || !LevelTimerConfig->FindTimeLimitForMap(
		UGameplayStatics::GetCurrentLevelName(this, true), Duration)) return;
	if (UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance()))
	{
		ExpectedLevelParticipants = MoveTemp(Instance->LevelTravelParticipants);
		Instance->LevelTravelParticipants.Reset();
	}
	bWaitingForLevelReady = true;
	GetWorldTimerManager().SetTimer(LevelReadyWaitHandle, this,
		&ATeamProject_MOUGameMode::CheckLevelReadyWait, 1.0f, true);
	CheckLevelReadyWait();
}

// [LEVELREADY-005] 소유 클라이언트의 준비 보고를 집계하여 시작 조건을 검사한다.
void ATeamProject_MOUGameMode::ReportLevelReady(ATeamProject_MOUPlayerController* PC)
{
	if (!bWaitingForLevelReady || !IsValid(PC) || !PC->GetPawn() || !PC->PlayerState
		|| !RequestedLevelPlayers.Contains(PC)) return;
	ReadyLevelPlayers.Add(PC);
	CheckLevelReadyWait();
}

// [LEVELREADY-007] 새 컨트롤러에 준비 요청을 보내고 전원 준비 시 타이머를 시작한다.
void ATeamProject_MOUGameMode::CheckLevelReadyWait()
{
	if (!bWaitingForLevelReady || bLevelTimerStarted) return;
	TSet<FString> ReadyKeys;
	int32 PlayerCount = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(It->Get());
		if (!PC || PC->bWaitForSafeLobby) continue;
		++PlayerCount;
		if (!ReadyLevelPlayers.Contains(PC))
		{
			RequestedLevelPlayers.Add(PC);
			PC->ClientBeginLevelReadyWait();
		}
		if (ReadyLevelPlayers.Contains(PC)) ReadyKeys.Add(PC->GetLevelReadyParticipantKey());
	}
	if (!bWaitingForLevelReady || bLevelTimerStarted || PlayerCount == 0 || GetWorld()->IsInSeamlessTravel()) return;
	for (const FString& Expected : ExpectedLevelParticipants)
		if (!ReadyKeys.Contains(Expected)) return;
	if (ReadyKeys.Num() < PlayerCount) return;
	bWaitingForLevelReady = false;
	bLevelTimerStarted = true;
	GetWorldTimerManager().ClearTimer(LevelReadyWaitHandle);
	TryStartLevelTimer();
}

// [SETTLEMENT-005] 접속 종료된 플레이어를 확인 대상에서 제거하고 남은 인원을 다시 검사합니다.
void ATeamProject_MOUGameMode::Logout(AController* Exiting)
{
	if (ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(Exiting))
	{
		ExpectedLevelParticipants.Remove(PC->GetLevelReadyParticipantKey());
		ReadyLevelPlayers.Remove(PC);
		RequestedLevelPlayers.Remove(PC);
	}
	for (auto It = LobbyEntryReadyMembers.CreateIterator(); It; ++It)
		if (It.Value().Get() == Exiting) It.RemoveCurrent();
	if (Exiting)
	{
		if (APlayerState* PlayerState = Exiting->GetPlayerState<APlayerState>())
		{
			ConfirmedSettlementPlayers.Remove(TWeakObjectPtr<APlayerState>(PlayerState));
		}
	}

	Super::Logout(Exiting);
	CheckLevelReadyWait();
	CheckAllPlayersConfirmedSettlement();
}

// [LOBBYLOAD-006] 예상 참여자의 소유 Pawn과 준비 보고를 검증하고 전원 준비를 집계합니다.
void ATeamProject_MOUGameMode::ReportLobbyEntryReady(ATeamProject_MOUPlayerController* PC, int64 UserId)
{
	UProjectGameInstanceBase* Instance = Cast<UProjectGameInstanceBase>(GetGameInstance());
	AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>();
	if (!Instance || !State || State->LobbyEntryPhase != 1 || !IsLobbyLevel()
		|| !IsValid(PC) || !PC->GetPawn() || !PC->PlayerState
		|| !Instance->LobbyEntryExpectedMembers.Contains(UserId)) return;
	for (const auto& Entry : LobbyEntryReadyMembers)
		if (Entry.Value.Get() == PC && Entry.Key != UserId) return;
	if (const auto* Existing = LobbyEntryReadyMembers.Find(UserId))
		if (Existing->IsValid() && Existing->Get() != PC) return;
	LobbyEntryReadyMembers.Add(UserId, PC);
	for (int64 Expected : Instance->LobbyEntryExpectedMembers)
	{
		const auto* Ready = LobbyEntryReadyMembers.Find(Expected);
		if (!Ready || !Ready->IsValid() || !Ready->Get()->GetPawn()) return;
	}
	State->LobbyEntryPhase = 2;
	State->ForceNetUpdate();
}

// [LATEJOIN-012] 지정된 안전구역을 판별하며 미설정 시 LobbyLevel을 사용합니다.
bool ATeamProject_MOUGameMode::IsLobbyLevel() const
{
	if (!GetWorld()) return false;
	if (LobbyMap.IsNull()) return UGameplayStatics::GetCurrentLevelName(this, true) == TEXT("LobbyLevel");
	const FString LobbyPackage = LobbyMap.ToSoftObjectPath().GetLongPackageName();
	return UGameplayStatics::GetCurrentLevelName(this, true) == FPackageName::GetShortName(LobbyPackage);
}

void ATeamProject_MOUGameMode::AdvanceHalfDay()
{
	AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>();
	if (!State || !RunState || !GameCycleState || RunState->RunPhase != ERunPhase::Playing) return;

	// 레벨 이동으로 복구된 경제 시간은 과거 일일 콜백을 다시 발생시키지 않습니다.
	const int32 PreviousEconomyTime = State->GetEconomyCurrentHalfDay();
	if (GameCycleState->EconomyTime != PreviousEconomyTime)
	{
		GameCycleState->InitializeEconomyTime(PreviousEconomyTime);
	}

	State->AdvanceEconomyHalfDay();
	GameCycleState->NotifyEconomyTimeAdvanced(State->GetEconomyCurrentHalfDay());
}

void ATeamProject_MOUGameMode::NotifyPlayerDeath()
{
	if (bKillingPlayersForLevelTimeout) return;
	CheckAllPlayersDead();
}

void ATeamProject_MOUGameMode::NotifyDebtPaymentFailed()
{
	FinishRun(ERunEndReason::DebtPaymentFailed);
}

bool ATeamProject_MOUGameMode::NotifyLevelSettlement(const FLevelSettlementData& Result)
{
	FLevelSettlementData EnrichedResult = Result;
	EnrichSettlementData(EnrichedResult);
	if (!LevelSettlementState || !LevelSettlementState->FinalizeSettlement(EnrichedResult))
	{
		return false;
	}

	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	if (LevelTimerState)
	{
		LevelTimerState->StopTimer();
	}
	return true;
}

void ATeamProject_MOUGameMode::TryStartLevelTimer()
{
	if (!LevelTimerConfig || !LevelTimerState) return;

	const FString CurrentMapName = UGameplayStatics::GetCurrentLevelName(this, true);
	float TimeLimitSeconds = 0.0f;
	if (!LevelTimerConfig->FindTimeLimitForMap(CurrentMapName, TimeLimitSeconds))
	{
		return;
	}

	LevelTimerState->StartTimer(TimeLimitSeconds);
	GetWorldTimerManager().SetTimer(LevelTimerUpdateHandle, this,
		&ATeamProject_MOUGameMode::UpdateLevelTimer,
		LevelTimerUpdateInterval, true);
}

void ATeamProject_MOUGameMode::UpdateLevelTimer()
{
	if (!RunState || !LevelTimerState || !LevelTimerState->Snapshot.bActive) return;

	RunState->SetThreatLevel(LevelTimerState->GetProgress());
	if (LevelTimerState->GetRemainingSeconds() <= 0.0f)
	{
		GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
		LevelTimerState->ExpireTimer();
		KillAllPlayersByTimeLimit();
	}
}

void ATeamProject_MOUGameMode::KillAllPlayersByTimeLimit()
{
	FinalizeFailedSettlement(ELevelSettlementReason::TimeExpired);
	bKillingPlayersForLevelTimeout = true;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		AMainCharacter* Character = *It;
		if (!Character->IsPlayerControlled() || Character->bIsDead) continue;

		// 기존 사망 Ability/드롭 흐름을 그대로 사용하되 그로기 단계를 건너뜁니다.
		Character->DownCount = FMath::Max(1, Character->DownCount);
		Character->HandleHealthZero();
	}
	bKillingPlayersForLevelTimeout = false;

	BeginLevelTimeoutSequence();
}

void ATeamProject_MOUGameMode::BeginLevelTimeoutSequence()
{
	if (!HasAuthority() || !RunState
		|| RunState->RunPhase == ERunPhase::GameOver
		|| RunState->RunPhase == ERunPhase::Resetting)
	{
		return;
	}

	// UI 연출이 끝날 때까지 현재 레벨에 머뭅니다. 아이템은 저장하지 않고 모두 유실시킵니다.
	RunState->SetRunState(ERunPhase::GameOver, ERunEndReason::LevelTimeExpired);
	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	DestroyPlayerOwnedItems();
}

void ATeamProject_MOUGameMode::CompleteLevelTimeoutSequence()
{
	if (!HasAuthority() || bTimeoutTravelStarted || !RunState)
	{
		return;
	}

	if (RunState->RunPhase != ERunPhase::GameOver
		|| RunState->RunEndReason != ERunEndReason::LevelTimeExpired)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CompleteLevelTimeoutSequence ignored: the run did not end by level timeout."));
		return;
	}

	bTimeoutTravelStarted = true;
	TravelToLobbyAfterTimeout();
}

// [SETTLEMENT-001] 성공 정산이 확정된 상태에서만 확인 또는 확인 취소를 반영합니다.
void ATeamProject_MOUGameMode::SetSettlementConfirmation(
	APlayerController* PlayerController, bool bConfirmed)
{
	if (!HasAuthority() || bSettlementTravelStarted || !PlayerController
		|| !LevelSettlementState || !LevelSettlementState->IsFinalized())
	{
		return;
	}

	const FLevelSettlementData Result = LevelSettlementState->GetResult();
	if (!Result.bSucceeded || Result.Reason != ELevelSettlementReason::Cleared)
	{
		return;
	}

	APlayerState* PlayerState = PlayerController->GetPlayerState<APlayerState>();
	if (!IsValid(PlayerState))
	{
		return;
	}

	const TWeakObjectPtr<APlayerState> PlayerKey(PlayerState);
	if (bConfirmed)
	{
		ConfirmedSettlementPlayers.Add(PlayerKey);
	}
	else
	{
		ConfirmedSettlementPlayers.Remove(PlayerKey);
	}

	CheckAllPlayersConfirmedSettlement(PlayerController);
}

// [SETTLEMENT-002] 현재 접속 중인 실제 플레이어 전원이 확인했는지 검사합니다.
void ATeamProject_MOUGameMode::CheckAllPlayersConfirmedSettlement(
	APlayerController* PreferredRequester)
{
	if (!HasAuthority() || bSettlementTravelStarted)
	{
		return;
	}

	const AGameStateBase* CurrentGameState = GetGameState<AGameStateBase>();
	if (!CurrentGameState)
	{
		return;
	}

	int32 ConnectedPlayerCount = 0;
	int32 ConfirmedPlayerCount = 0;
	APlayerController* TravelRequester = nullptr;
	APlayerController* FirstConfirmedController = nullptr;
	for (APlayerState* PlayerState : CurrentGameState->PlayerArray)
	{
		if (!IsValid(PlayerState) || PlayerState->IsOnlyASpectator())
		{
			continue;
		}

		++ConnectedPlayerCount;
		if (ConfirmedSettlementPlayers.Contains(TWeakObjectPtr<APlayerState>(PlayerState)))
		{
			++ConfirmedPlayerCount;
			APlayerController* PlayerController = PlayerState->GetPlayerController();
			if (!FirstConfirmedController)
			{
				FirstConfirmedController = PlayerController;
			}
			if (PlayerController == PreferredRequester)
			{
				TravelRequester = PlayerController;
			}
		}
	}

	if (ConnectedPlayerCount > 0 && ConfirmedPlayerCount == ConnectedPlayerCount)
	{
		CompleteSettlementSequence(TravelRequester ? TravelRequester : FirstConfirmedController);
	}
}

// [SETTLEMENT-003] 성공한 정상 정산에 대해서만 BP의 기존 저장 및 이동 흐름을 한 번 실행합니다.
void ATeamProject_MOUGameMode::CompleteSettlementSequence(APlayerController* TravelRequester)
{
	if (!HasAuthority() || bSettlementTravelStarted
		|| !IsValid(TravelRequester)
		|| !LevelSettlementState || !LevelSettlementState->IsFinalized())
	{
		return;
	}

	const FLevelSettlementData Result = LevelSettlementState->GetResult();
	if (!Result.bSucceeded || Result.Reason != ELevelSettlementReason::Cleared)
	{
		return;
	}

	bSettlementTravelStarted = true;
	OnAllPlayersConfirmedSettlement(TravelRequester);
}

void ATeamProject_MOUGameMode::CheckAllPlayersDead()
{
	bool bFoundPlayer = false;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerControlled()) continue;
		bFoundPlayer = true;
		if (!It->bIsDead) return;
	}
	if (bFoundPlayer)
	{
		FinalizeFailedSettlement(ELevelSettlementReason::AllPlayersDead);
		FinishRun(ERunEndReason::AllPlayersDead);
	}
}

void ATeamProject_MOUGameMode::FinalizeFailedSettlement(ELevelSettlementReason Reason)
{
	if (!LevelSettlementState || LevelSettlementState->IsFinalized()) return;

	FLevelSettlementData Result;
	Result.Reason = Reason;
	Result.bSucceeded = false;
	EnrichSettlementData(Result);
	Result.FinalThreatLevel = RunState ? RunState->ThreatLevel : 0.0f;
	if (LevelTimerState)
	{
		Result.LevelPlayTimeSeconds = LevelTimerState->Snapshot.DurationSeconds
			- LevelTimerState->GetRemainingSeconds();
	}
	LevelSettlementState->FinalizeSettlement(Result);
}

void ATeamProject_MOUGameMode::EnrichSettlementData(FLevelSettlementData& Result) const
{
	TArray<FPlayerSettlementData> DeliveryPlayerResults;
	if (DeliveryManager)
	{
		const FDeliveryProgress& Delivery = DeliveryManager->Progress;
		Result.EarnedGold = Delivery.EarnedGold;
		Result.DeliveredItemCount = Delivery.DeliveredItemCount;
		Result.FailedDeliveryCount = Delivery.BrokenItemCount;
		Result.DeliveredItems = Delivery.DeliveredItems;
		DeliveryPlayerResults = Delivery.PlayerResults;
	}

	// 정산 UI의 인원은 누적 기록이 아니라 현재 월드에 접속해 있는 플레이어를 기준으로 만든다.
	// 배달 시점과 정산 시점 사이에 PlayerName이 바뀌어도 유효한 PlayerId가 같으면 동일인이다.
	Result.PlayerResults.Reset();
	Result.DeathCount = 0;
	Result.KnockdownCount = 0;
	for (TActorIterator<AMainCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerControlled()) continue;
		const int32 PlayerDeathCount = It->bIsDead ? 1 : 0;
		Result.DeathCount += PlayerDeathCount;
		Result.KnockdownCount += It->KnockdownCount;

		const APlayerState* PlayerState = It->GetPlayerState();
		const int32 PlayerId = PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE;
		const FString PlayerName = PlayerState ? PlayerState->GetPlayerName() : It->GetName();
		int32 PlayerIndex = Result.PlayerResults.IndexOfByPredicate(
			[PlayerId, &PlayerName](const FPlayerSettlementData& Player)
			{
				return PlayerId != INDEX_NONE
					? Player.PlayerId == PlayerId
					: Player.PlayerName == PlayerName;
			});
		if (PlayerIndex == INDEX_NONE)
		{
			const FPlayerSettlementData* DeliveryResult = DeliveryPlayerResults.FindByPredicate(
				[PlayerId, &PlayerName](const FPlayerSettlementData& Player)
				{
					return PlayerId != INDEX_NONE
						? Player.PlayerId == PlayerId
						: Player.PlayerName == PlayerName;
				});
			FPlayerSettlementData Player = DeliveryResult
				? *DeliveryResult
				: FPlayerSettlementData();
			Player.PlayerId = PlayerId;
			Player.PlayerName = PlayerName;
			PlayerIndex = Result.PlayerResults.Add(MoveTemp(Player));
		}
		Result.PlayerResults[PlayerIndex].KnockdownCount = It->KnockdownCount;
	}

	Result.LootedItems.Reset();
	Result.LootedItemCount = 0;
	if (const UWarehouseDataSubsystem* WarehouseSubsystem =
		GetGameInstance()->GetSubsystem<UWarehouseDataSubsystem>())
	{
		for (const FStoredItemData& LootedItem : WarehouseSubsystem->GetLastLootedItemsCopy())
		{
			if (!LootedItem.ItemClass || LootedItem.Quantity <= 0) continue;

			FSettlementItemEntry Entry;
			Entry.ItemClass = LootedItem.ItemClass;
			Entry.Quantity = LootedItem.Quantity;
			if (const AItemBase* ItemDefaults = LootedItem.ItemClass->GetDefaultObject<AItemBase>())
			{
				Entry.ItemName = ItemDefaults->ItemName;
				Entry.ItemIcon = ItemDefaults->ItemIcon;
			}
			Result.LootedItemCount += Entry.Quantity;
			Result.LootedItems.Add(MoveTemp(Entry));
		}

		for (const FPlayerSettlementData& LootResult : WarehouseSubsystem->GetLastPlayerLootResultsCopy())
		{
			int32 PlayerIndex = Result.PlayerResults.IndexOfByPredicate(
				[&LootResult](const FPlayerSettlementData& Player)
				{
					return LootResult.PlayerId != INDEX_NONE
						? Player.PlayerId == LootResult.PlayerId
						: Player.PlayerName == LootResult.PlayerName;
				});
			if (PlayerIndex != INDEX_NONE)
			{
				Result.PlayerResults[PlayerIndex].LootedItemCount = LootResult.LootedItemCount;
				Result.PlayerResults[PlayerIndex].LootedItems = LootResult.LootedItems;
			}
		}
	}
}

void ATeamProject_MOUGameMode::FinishRun(ERunEndReason Reason)
{
	if (!RunState || RunState->RunPhase == ERunPhase::GameOver || RunState->RunPhase == ERunPhase::Resetting) return;

	RunState->SetRunState(ERunPhase::GameOver, Reason);
	GetWorldTimerManager().ClearTimer(LevelTimerUpdateHandle);
	DestroyPlayerOwnedItems();

	if (Reason == ERunEndReason::AllPlayersDead)
	{
		TravelToLobbyAfterWipe();
		return;
	}
	if (Reason == ERunEndReason::LevelTimeExpired)
	{
		TravelToLobbyAfterTimeout();
		return;
	}

	OnRunGameOver(Reason);
	GetWorldTimerManager().SetTimer(ResetTimerHandle, this,
		&ATeamProject_MOUGameMode::ResetRunToDayOne,
		FMath::Max(GameOverResetDelay, KINDA_SMALL_NUMBER), false);
}

void ATeamProject_MOUGameMode::DestroyPlayerOwnedItems()
{
	for (TActorIterator<AItemBase> It(GetWorld()); It; ++It)
	{
		AItemBase* Item = *It;
		if (Item && IsValid(Item->LastOwner) && Item->LastOwner->IsA<AMainCharacter>())
		{
			Item->Destroy();
		}
	}
}

void ATeamProject_MOUGameMode::ResetRunToDayOne()
{
	if (RunState)
	{
		RunState->SetRunState(ERunPhase::Resetting, RunState->RunEndReason);
	}
	if (UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>())
	{
		GameInstance->ResetRunData();
	}
	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	GetWorld()->ServerTravel(CurrentLevelName, false);
}

// [RUN-000] 플레이어 전멸 시 영속 런 데이터를 보존하고 로비로 즉시 이동합니다.
void ATeamProject_MOUGameMode::TravelToLobbyAfterWipe()
{
	const FSoftObjectPath LobbyPath = LobbyMap.ToSoftObjectPath();
	const FString LobbyPackageName = LobbyPath.GetLongPackageName();
	if (LobbyPackageName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("LobbyMap is not configured. Cannot travel after party wipe."));
		return;
	}

	RunState->SetRunState(ERunPhase::Resetting, RunState->RunEndReason);
	GetWorld()->ServerTravel(LobbyPackageName, false);
}

void ATeamProject_MOUGameMode::TravelToLobbyAfterTimeout()
{
	const FSoftObjectPath LobbyPath = LobbyMap.ToSoftObjectPath();
	const FString LobbyPackageName = LobbyPath.GetLongPackageName();
	if (LobbyPackageName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("LobbyMap is not configured. Cannot travel after level timeout."));
		return;
	}

	// 타임아웃도 해당 HalfDay를 소비한다. 런 데이터는 초기화하지 않고 증가한 경제 상태를
	// GameInstance에 저장해 로비의 새 GameState가 그대로 복원하도록 한다.
	if (AProjectGameStateBase* State = GetGameState<AProjectGameStateBase>())
	{
		State->AdvanceEconomyHalfDay();
		if (GameCycleState)
		{
			GameCycleState->NotifyEconomyTimeAdvanced(State->GetEconomyCurrentHalfDay());
		}
	}
	if (UProjectGameInstanceBase* GameInstance = GetGameInstance<UProjectGameInstanceBase>())
	{
		GameInstance->ClearPendingDeliveryData();
		GameInstance->SaveEconomyData();
	}

	RunState->SetRunState(ERunPhase::Resetting, RunState->RunEndReason);
	GetWorld()->ServerTravel(LobbyPackageName, false);
}

// [LATEJOIN-005] 새 접속의 식별자로 안전구역 합류 여부를 결정합니다.
FString ATeamProject_MOUGameMode::InitNewPlayer(APlayerController* NewPlayerController,
    const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    if (Error.IsEmpty())
    {
        if (ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(NewPlayerController))
        {
            PC->PlaySessionId = UGameplayStatics::ParseOption(Options, TEXT("MOUPlaySession"));
            if (UServerSubsystem* Server = GetGameInstance()->GetSubsystem<UServerSubsystem>())
            {
                bool bAllowDirectEditorPlay = false;
#if WITH_EDITOR
                // 로비 방에 참가하지 않고 PIE로 직접 실행한 경우에만 참가를 허용합니다.
                bAllowDirectEditorPlay = GetWorld()
                    && GetWorld()->WorldType == EWorldType::PIE
                    && Server->GetCurrentRoomId() == 0;
#endif

                PC->bWaitForSafeLobby = !PC->IsLocalController()
                    && !Server->AdmitPlaySession(
                        PC->PlaySessionId,
                        IsLobbyLevel() || bAllowDirectEditorPlay);
            }
        }
    }
    return Error;
}

// [LATEJOIN-006] 일반 및 심리스 이동 후 관전 제한을 적용하거나 안전구역에서 해제합니다.
void ATeamProject_MOUGameMode::GenericPlayerInitialization(AController* C)
{
    if (ATeamProject_MOUPlayerController* PC = Cast<ATeamProject_MOUPlayerController>(C))
    {
        if (IsLobbyLevel())
        {
            PC->bWaitForSafeLobby = false;
            if (UServerSubsystem* Server = GetGameInstance()->GetSubsystem<UServerSubsystem>())
                Server->AdmitPlaySession(PC->PlaySessionId, true);
        }
        if (PC->PlayerState)
        {
            PC->PlayerState->SetIsOnlyASpectator(PC->bWaitForSafeLobby);
            PC->PlayerState->SetIsSpectator(PC->bWaitForSafeLobby);
        }
        if (PC->bWaitForSafeLobby) PC->StartSpectatingOnly();
    }
    Super::GenericPlayerInitialization(C);
}
