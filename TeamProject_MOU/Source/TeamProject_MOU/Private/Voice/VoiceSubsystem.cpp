// MOU 음성 - 음성 시스템 진입점 구현 + 콘솔 명령.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7절, 11절, 14절(마일스톤)
//
// [스레드]
//   이 파일의 코드는 전부 게임 스레드다.
//   마이크 캡처도 게임 스레드다(VoiceCaptureSource.h 상단 주석에 이유가 있다).
//   유일한 다른 스레드는 오디오 렌더 스레드이고 VoiceSynthComponent.cpp 가 담당한다.

#include "Voice/VoiceSubsystem.h"

#include "Voice/VoiceCaptureSource.h"
#include "Voice/VoiceSynthComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Modules/ModuleManager.h"
#include "Perception/AISense_Hearing.h"
#include "VoiceModule.h"

// ---------------------------------------------------------------------------
// 서브시스템 수명
// ---------------------------------------------------------------------------

void UVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ★ Voice 모듈을 여기(게임 스레드)에서 명시적으로 로드한다.
	//
	//   Build.cs 에 의존성을 걸어도 그건 **링크**만 보장한다. 런타임에 모듈이
	//   실제로 로드돼 있는지는 별개다. FVoiceModule::IsAvailable() 은
	//   IsModuleLoaded("Voice") 라서, 아무도 안 불렀으면 false 를 돌려준다.
	//   그 상태로 캡처를 만들려 하면 "마이크가 없다" 로 오진하게 된다.
	//
	//   모듈 로드는 게임 스레드에서만 해야 하므로 워커가 아니라 여기서 한다.
	//   FVoiceModule::StartupModule() 이 ini 를 읽고 캡처 장치를 초기화한다.
	FModuleManager::Get().LoadModule(TEXT("Voice"));

	if (!FVoiceModule::IsAvailable())
	{
		UE_LOG(LogMOUVoice, Error,
			TEXT("Voice 모듈을 로드하지 못했다. 음성 기능을 사용할 수 없다."));
	}
	else if (!FVoiceModule::Get().IsVoiceEnabled())
	{
		// 여기 걸리면 십중팔구 ini 문제다. 진단 문구를 구체적으로 남긴다.
		UE_LOG(LogMOUVoice, Error,
			TEXT("Voice 모듈은 로드됐지만 비활성 상태다. ")
			TEXT("Config/DefaultEngine.ini 에 [Voice] bEnabled=true 가 있는지, ")
			TEXT("그리고 에디터를 재시작했는지 확인할 것(ini 는 시작 시 한 번만 읽는다)."));
	}
	else
	{
		// 마이크 열기. 실패해도 게임은 정상 진행된다(재생만 동작).
		CaptureSource = MakeUnique<FVoiceCaptureSource>();
		CaptureSource->Start();
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UVoiceSubsystem::Tick));

	UE_LOG(LogMOUVoice, Log,
		TEXT("음성 서브시스템 초기화 완료 (마이크=%s). 루프백을 켜려면 MOU.Voice.Loopback 1"),
		IsCaptureReady() ? TEXT("준비됨") : TEXT("없음"));
}

void UVoiceSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	// ★ 순서가 중요하다: 재생을 먼저 멈추고 나서 캡처를 정리한다.
	//   반대로 하면 오디오 렌더 스레드가 아직 도는 동안 버퍼가 사라질 수 있다.
	//
	// IsValid() 로 검사하는 이유: PlayerController 가 먼저 파괴되면 컴포넌트도
	// 같이 파괴되지만, UPROPERTY 포인터는 GC 가 돌기 전까지 null 이 되지 않는다.
	// 그 사이에 Deinitialize 가 오면 이미 죽은 컴포넌트를 만지게 된다.
	if (IsValid(PlaybackComponent))
	{
		PlaybackComponent->Stop();
		PlaybackComponent->DestroyComponent();
	}
	PlaybackComponent = nullptr;

	// 마이크를 닫는다. 게임 스레드에서만 도는 객체라 스레드 종료 대기가 필요 없다.
	// (워커 스레드였을 때는 Kill(true) 순서가 필수였지만 이제 해당 없음)
	CaptureSource.Reset();

	Super::Deinitialize();
}

UVoiceSubsystem* UVoiceSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;

	return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// 틱 - 워커 큐를 비워 재생으로 넘긴다
// ---------------------------------------------------------------------------

bool UVoiceSubsystem::Tick(float DeltaTime)
{
	if (!CaptureSource)
	{
		return true;
	}

	// 마이크를 폴링한다. 게임 스레드에서 부르는 것이 필수다(헤더 주석 참고).
	//
	// ★ 음소거 중에도 Poll() 자체는 부른다.
	//   설계 문서는 "캡처 자체를 중단한다" 고 적었지만, 여기서 아예 안 부르면
	//   엔진의 내부 캡처 버퍼(우리가 안 가져가는 동안 계속 쌓인다)가 얼마나
	//   쌓일지 우리가 통제할 수 없다 - 음소거를 오래 켜뒀다가 풀면 그동안
	//   쌓인 오디오가 한꺼번에 쏟아지는 것을 배제할 수 없다.
	//   그래서 Poll 은 계속 해서 엔진 버퍼를 비우되, **결과를 밑에서 버린다.**
	//   "말하는 상태가 밖으로 전혀 안 나간다" 는 결과는 동일하게 보장된다.
	PolledFrames.Reset();
	CaptureSource->Poll(PolledFrames);

	if (bMuted)
	{
		// 밖에서 보기엔 마이크가 꺼진 것과 같다: 발화 상태 강제 OFF, 재생/전송 없음.
		bIsSpeaking = false;
		return true;
	}

	const bool bReceivedAnyFrame = PolledFrames.Num() > 0;

	for (const FMOUVoiceFrame& Frame : PolledFrames)
	{
		++FramesReceived;
		bIsSpeaking = Frame.bIsSpeaking;

		if (!bLoopbackEnabled || !PlaybackComponent)
		{
			// 루프백이 꺼져 있으면 프레임을 그냥 버린다.
			// V3 에서는 여기가 "서버로 전송" 으로 바뀐다.
			continue;
		}

		// 무음 구간은 재생하지 않는다. 대역폭 절감이자, 루프백에서 마이크
		// 잡음이 계속 되돌아오는 것을 막는다.
		if (!Frame.bIsSpeaking)
		{
			continue;
		}

		// 지연이 쌓이는 것을 막는다.
		//
		// 마이크 입력이 재생 소비보다 빠르면 버퍼가 계속 차서 "지금 말한 것이
		// 몇 초 뒤에 들리는" 상태가 된다. 임계치를 넘으면 버퍼를 비워
		// 현재 시점으로 되돌린다. 음성은 오래된 것을 버리는 게 맞다.
		const int32 BufferedFrames =
			PlaybackComponent->GetBufferedSampleCount() / MOUVoice::SamplesPerFrame;

		if (BufferedFrames >= MOUVoice::PlaybackDropThresholdFrames)
		{
			PlaybackComponent->RequestFlush();
			++FramesDropped;
		}

		PlaybackComponent->PushSamples(Frame.Samples.GetData(), Frame.Samples.Num());
	}

	// ★ 프레임이 아예 안 오는 경우에도 발화 상태를 내려야 한다.
	//
	// 엔진 캡처는 무음 구간에 데이터를 아예 주지 않을 수 있다(내부 노이즈 게이트).
	// 그러면 위 루프가 한 번도 안 돌아 bIsSpeaking 이 마지막 값(true)에 그대로
	// 붙박인다. 그 상태로 V8 의 소음 이벤트가 붙으면 "말을 멈췄는데 NPC 가
	// 계속 쫓아오는" 버그가 된다. 마이크가 뽑히거나 캡처가 죽어도 마찬가지다.
	const double Now = FPlatformTime::Seconds();
	if (bReceivedAnyFrame)
	{
		LastFrameTime = Now;
	}
	else if (bIsSpeaking && LastFrameTime > 0.0
		&& (Now - LastFrameTime) > MOUVoice::VadHangoverSeconds)
	{
		bIsSpeaking = false;
	}

	DrawRadiusDebug();

	return true; // 계속 틱
}

void UVoiceSubsystem::DrawRadiusDebug()
{
#if ENABLE_DRAW_DEBUG
	// 말하고 있을 때만 그린다. 조용할 때도 계속 그리면 "지금 소리가 나가는가" 를
	// 눈으로 구분할 수 없어서 시각화의 의미가 없어진다.
	if (!bShowRadiusDebug || !bIsSpeaking)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (!Pawn)
	{
		return;
	}

	// 발 밑에서 살짝 띄운다. 바닥과 정확히 같은 높이면 지면에 파묻혀 잘 안 보인다.
	const FVector Center = Pawn->GetActorLocation() - FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() - 2.f);

	// XY 평면(바닥과 평행)에 그리기 위한 축. 기본값은 세로 원이라 바꿔줘야 한다.
	const FVector AxisX(1.f, 0.f, 0.f);
	const FVector AxisY(0.f, 1.f, 0.f);

	constexpr int32 Segments = 48;
	constexpr float LifeTime = -1.f;  // 한 프레임만. 매 틱 다시 그린다
	constexpr uint8 Depth    = 0;

	// ★ 이 두 값은 V8 의 ReportNoiseEvent 가 쓸 값과 같은 함수에서 나온다.
	//   그래서 화면에 보이는 원이 곧 실제 판정 범위다(VoiceTypes.h 참고).
	const float HearRadius = MOUVoice::GetHearRadius(VoiceMode);
	const float NoiseRange = MOUVoice::GetNoiseRange(VoiceMode);

	// 초록 = 사람이 듣는 거리
	DrawDebugCircle(World, Center, HearRadius, Segments, FColor::Green,
		false, LifeTime, Depth, 4.f, AxisX, AxisY, /*bDrawAxis=*/false);

	// 빨강 = NPC 가 듣는 거리. 일부러 더 넓다(13절).
	DrawDebugCircle(World, Center, NoiseRange, Segments, FColor::Red,
		false, LifeTime, Depth, 4.f, AxisX, AxisY, /*bDrawAxis=*/false);

	// 지금 어떤 모드로 말하는 중인지 머리 위에 띄운다.
	// 링만 있으면 두 원 중 어느 쪽이 어느 모드인지 헷갈린다.
	DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.f, 0.f, 100.f),
		FString::Printf(TEXT("%s  들림 %.0fm / NPC %.0fm  (음량 %.2f)"),
			MOUVoice::GetVoiceModeName(VoiceMode),
			HearRadius / 100.f, NoiseRange / 100.f, GetCurrentLoudness()),
		nullptr, FColor::White, 0.f /*이번 프레임만*/);
#endif
}

// ---------------------------------------------------------------------------
// 루프백
// ---------------------------------------------------------------------------

void UVoiceSubsystem::EnsurePlaybackComponent()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;

	if (!World || !PC)
	{
		UE_LOG(LogMOUVoice, Warning,
			TEXT("재생 컴포넌트를 만들 수 없다. 아직 PlayerController 가 없다(레벨 로드 중일 수 있다)."));
		return;
	}

	// 레벨을 이동하면 기존 컴포넌트는 죽은 월드에 남는다.
	// LocalPlayer 는 트래블을 넘어 살아있으므로 여기서 다시 만들어야 한다.
	if (PlaybackComponent && (!IsValid(PlaybackComponent) || PlaybackComponent->GetWorld() != World))
	{
		if (IsValid(PlaybackComponent))
		{
			PlaybackComponent->Stop();
			PlaybackComponent->DestroyComponent();
		}
		PlaybackComponent = nullptr;
	}

	if (PlaybackComponent)
	{
		return;
	}

	// PlayerController 를 소유자로 삼는다.
	// 폰에 붙이면 죽거나 리스폰할 때 같이 사라지는데, 음성은 그와 무관하게
	// 유지돼야 한다(관전 중 무전 수신 등, V6 이후).
	PlaybackComponent = NewObject<UVoiceSynthComponent>(PC, TEXT("MOUVoicePlayback"));
	PlaybackComponent->RegisterComponent();
	PlaybackComponent->Start();

	UE_LOG(LogMOUVoice, Log, TEXT("음성 재생 컴포넌트 생성 완료."));
}

void UVoiceSubsystem::SetLoopbackEnabled(bool bEnabled)
{
	if (bLoopbackEnabled == bEnabled)
	{
		return;
	}

	bLoopbackEnabled = bEnabled;

	if (bEnabled)
	{
		EnsurePlaybackComponent();

		if (!PlaybackComponent)
		{
			bLoopbackEnabled = false;
			return;
		}

		// 껐다 켜는 사이에 남은 옛 소리를 버린다.
		PlaybackComponent->RequestFlush();

		if (!IsCaptureReady())
		{
			UE_LOG(LogMOUVoice, Warning,
				TEXT("루프백을 켰지만 마이크가 준비되지 않았다. 소리가 나지 않는다."));
		}
		else
		{
			UE_LOG(LogMOUVoice, Log,
				TEXT("루프백 ON. ★헤드폰을 쓸 것 - 스피커로 들으면 하울링이 난다."));
		}
	}
	else
	{
		if (PlaybackComponent)
		{
			PlaybackComponent->RequestFlush();
		}
		UE_LOG(LogMOUVoice, Log, TEXT("루프백 OFF."));
	}
}

void UVoiceSubsystem::SetVoiceMode(EVoiceMode NewMode)
{
	if (VoiceMode == NewMode)
	{
		return;
	}

	VoiceMode = NewMode;

	UE_LOG(LogMOUVoice, Log, TEXT("발화 모드 = %s (들림 %.0fcm / NPC %.0fcm)"),
		MOUVoice::GetVoiceModeName(VoiceMode),
		MOUVoice::GetHearRadius(VoiceMode),
		MOUVoice::GetNoiseRange(VoiceMode));
}

void UVoiceSubsystem::SetShowRadiusDebug(bool bEnabled)
{
	bShowRadiusDebug = bEnabled;
	UE_LOG(LogMOUVoice, Log, TEXT("소리 범위 표시 %s. %s"),
		bEnabled ? TEXT("ON") : TEXT("OFF"),
		bEnabled ? TEXT("말하는 동안에만 링이 보인다(초록=사람, 빨강=NPC).") : TEXT(""));
}

void UVoiceSubsystem::SetMuted(bool bInMuted)
{
	if (bMuted == bInMuted)
	{
		return;
	}

	bMuted = bInMuted;

	if (bMuted)
	{
		// 즉시 조용해진다. 이미 재생 버퍼에 들어간 소리가 꼬리처럼 남는 것을 막는다.
		bIsSpeaking = false;
		if (PlaybackComponent)
		{
			PlaybackComponent->RequestFlush();
		}
	}

	UE_LOG(LogMOUVoice, Log, TEXT("마이크 %s."), bMuted ? TEXT("음소거") : TEXT("음소거 해제"));
}

bool UVoiceSubsystem::IsCaptureReady() const
{
	return CaptureSource.IsValid() && CaptureSource->IsReady();
}

float UVoiceSubsystem::GetCurrentLoudness() const
{
	return CaptureSource.IsValid() ? CaptureSource->GetCurrentLoudness() : 0.f;
}

void UVoiceSubsystem::SetMicSensitivity(float InThreshold)
{
	if (CaptureSource.IsValid())
	{
		CaptureSource->SetVadThreshold(InThreshold);
		UE_LOG(LogMOUVoice, Log, TEXT("마이크 감도(VAD 임계값) = %.4f"), InThreshold);
	}
}

float UVoiceSubsystem::GetMicSensitivity() const
{
	return CaptureSource.IsValid() ? CaptureSource->GetVadThreshold() : MOUVoice::DefaultVadThreshold;
}

FString UVoiceSubsystem::GetStatsString() const
{
	const int32 Buffered = PlaybackComponent ? PlaybackComponent->GetBufferedSampleCount() : 0;
	const int32 Underrun = PlaybackComponent ? PlaybackComponent->GetUnderrunCount() : 0;
	const int32 Overflow = PlaybackComponent ? PlaybackComponent->GetOverflowCount() : 0;

	return FString::Printf(
		TEXT("마이크=%s 음소거=%s 루프백=%s 발화=%s 모드=%s(들림%.0f/NPC%.0f) 감도=%.4f 음량=%.4f ")
		TEXT("| 수신프레임=%d 버림=%d ")
		TEXT("| 재생버퍼=%d샘플(%.0fms) 언더런=%d 오버플로=%d"),
		IsCaptureReady() ? TEXT("준비됨") : TEXT("없음"),
		bMuted ? TEXT("ON") : TEXT("OFF"),
		bLoopbackEnabled ? TEXT("ON") : TEXT("OFF"),
		bIsSpeaking ? TEXT("O") : TEXT("X"),
		MOUVoice::GetVoiceModeName(VoiceMode),
		MOUVoice::GetHearRadius(VoiceMode),
		MOUVoice::GetNoiseRange(VoiceMode),
		GetMicSensitivity(),
		GetCurrentLoudness(),
		FramesReceived,
		FramesDropped,
		Buffered,
		Buffered * 1000.f / MOUVoice::SampleRate,
		Underrun,
		Overflow);
}

// ---------------------------------------------------------------------------
// 콘솔 명령
//
// 사용 예:
//   MOU.Voice.Loopback 1          내 목소리를 내 헤드폰으로 (V1 검증)
//   MOU.Voice.Stat                통계 출력
//   MOU.Voice.Sensitivity 0.01    마이크 감도
//   MOU.Voice.FakeNoise 1500      마이크 없이 NPC 소음만 발생 (V0, NPC 팀원용)
//
// 기존 채팅의 MOU.Chat.* 와 같은 등록 방식을 쓴다.
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * 콘솔 명령이 실행된 월드에서 음성 서브시스템을 찾는다.
	 * PIE 창이 여러 개면 "지금 콘솔을 연 창" 의 것이 잡힌다.
	 */
	UVoiceSubsystem* FindVoiceSubsystem(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UVoiceSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GVoiceLoopbackCommand(
		TEXT("MOU.Voice.Loopback"),
		TEXT("로컬 루프백을 켜고 끈다(내 목소리가 내 헤드폰으로). 사용법: MOU.Voice.Loopback <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true;
					Voice->SetLoopbackEnabled(bEnable);
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceMuteCommand(
		TEXT("MOU.Voice.Mute"),
		TEXT("마이크 음소거를 켜고 끈다(C 키와 동일). 인자 없이 부르면 토글. 사용법: MOU.Voice.Mute [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (Args.IsValidIndex(0))
					{
						Voice->SetMuted(FCString::Atoi(*Args[0]) != 0);
					}
					else
					{
						Voice->ToggleMute();
					}
				}
				else
				{
					UE_LOG(LogMOUVoice, Warning, TEXT("음성 서브시스템을 찾지 못했다."));
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceShowRadiusCommand(
		TEXT("MOU.Voice.ShowRadius"),
		TEXT("말할 때 소리 도달 범위를 링으로 그린다(초록=사람, 빨강=NPC). 사용법: MOU.Voice.ShowRadius <0|1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0) ? (FCString::Atoi(*Args[0]) != 0) : true;
					Voice->SetShowRadiusDebug(bEnable);
				}
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceModeCommand(
		TEXT("MOU.Voice.Mode"),
		TEXT("발화 모드를 바꾼다. 사용법: MOU.Voice.Mode <0=속삭임|1=보통|2=외침>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				if (!Voice)
				{
					return;
				}

				if (!Args.IsValidIndex(0))
				{
					UE_LOG(LogMOUVoice, Log, TEXT("현재 발화 모드 = %s"),
						MOUVoice::GetVoiceModeName(Voice->GetVoiceMode()));
					return;
				}

				// 범위를 벗어난 값은 조용히 뭉개지 말고 알려준다.
				// 조용히 '보통'으로 떨어뜨리면 왜 안 바뀌는지 알 수 없다.
				const int32 Raw = FCString::Atoi(*Args[0]);
				if (Raw < 0 || Raw > 2)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("모드는 0(속삭임) / 1(보통) / 2(외침) 중 하나여야 한다. 받은 값: %d"), Raw);
					return;
				}

				Voice->SetVoiceMode(static_cast<EVoiceMode>(Raw));
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceStatCommand(
		TEXT("MOU.Voice.Stat"),
		TEXT("음성 파이프라인 통계를 출력한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					UE_LOG(LogMOUVoice, Log, TEXT("%s"), *Voice->GetStatsString());
				}
			}));

	/**
	 * 마이크가 안 잡힐 때 원인을 단계별로 짚어주는 명령.
	 *
	 * "마이크가 준비되지 않았다" 는 원인이 네 가지나 되는데 증상이 똑같아서
	 * 어디서 막혔는지 알기 어렵다. 각 단계를 따로 찍어준다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceDiagCommand(
		TEXT("MOU.Voice.Diag"),
		TEXT("마이크가 안 잡힐 때 원인을 단계별로 진단한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& /*Args*/, UWorld* World)
			{
				UE_LOG(LogMOUVoice, Log, TEXT("===== 음성 진단 ====="));

				const bool bModuleLoaded = FVoiceModule::IsAvailable();
				UE_LOG(LogMOUVoice, Log, TEXT("1) Voice 모듈 로드   : %s"),
					bModuleLoaded ? TEXT("O") : TEXT("X  <- 모듈이 안 올라왔다"));

				if (!bModuleLoaded)
				{
					UE_LOG(LogMOUVoice, Log, TEXT("   -> Build.cs 의 \"Voice\" 의존성을 확인할 것."));
					return;
				}

				const bool bVoiceEnabled = FVoiceModule::Get().IsVoiceEnabled();
				UE_LOG(LogMOUVoice, Log, TEXT("2) [Voice] bEnabled  : %s"),
					bVoiceEnabled ? TEXT("true") : TEXT("false <- ini 설정 문제"));

				if (!bVoiceEnabled)
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> Config/DefaultEngine.ini 에 [Voice] bEnabled=true 를 넣고 ")
						TEXT("**에디터를 재시작**할 것. ini 는 시작 시 한 번만 읽는다."));
					return;
				}

				UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				UE_LOG(LogMOUVoice, Log, TEXT("3) 음성 서브시스템   : %s"),
					Voice ? TEXT("O") : TEXT("X"));

				if (!Voice)
				{
					return;
				}

				UE_LOG(LogMOUVoice, Log, TEXT("4) 마이크 열기       : %s"),
					Voice->IsCaptureReady() ? TEXT("O") : TEXT("X  <- 장치 또는 권한 문제"));

				if (!Voice->IsCaptureReady())
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> Windows 설정 > 개인 정보 및 보안 > 마이크 에서 ")
						TEXT("'데스크톱 앱이 마이크에 액세스하도록 허용'이 켜져 있는지 확인할 것."));
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> 헤드셋을 꽂은 뒤 에디터를 켰다면, 장치 목록이 갱신되도록 ")
						TEXT("에디터를 재시작해볼 것."));
					return;
				}

				UE_LOG(LogMOUVoice, Log, TEXT("모두 정상. %s"), *Voice->GetStatsString());
			}));

	FAutoConsoleCommandWithWorldAndArgs GVoiceSensitivityCommand(
		TEXT("MOU.Voice.Sensitivity"),
		TEXT("마이크 감도(VAD 임계값)를 바꾼다. 사용법: MOU.Voice.Sensitivity <0~1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					if (!Args.IsValidIndex(0))
					{
						UE_LOG(LogMOUVoice, Log, TEXT("현재 감도 = %.4f"), Voice->GetMicSensitivity());
						return;
					}
					Voice->SetMicSensitivity(FCString::Atof(*Args[0]));
				}
			}));

	// -----------------------------------------------------------------------
	// V0 - NPC 담당 팀원 전용
	//
	// ★ 이 명령은 음성 파이프라인과 완전히 독립적이다.
	//   마이크가 없어도, 캡처가 실패해도, 음성 코드가 한 줄도 안 돌아도 동작한다.
	//   그래서 NPC 담당자는 음성 시스템 완성을 기다리지 않고 청각 반응을
	//   지금 바로 작업할 수 있다.
	//
	// V8 에서 진짜 음성이 붙으면 같은 ReportNoiseEvent 를 부르므로,
	// 이 명령으로 맞춰둔 NPC 동작이 그대로 유효하다.
	// -----------------------------------------------------------------------
	FAutoConsoleCommandWithWorldAndArgs GVoiceFakeNoiseCommand(
		TEXT("MOU.Voice.FakeNoise"),
		TEXT("마이크 없이 내 위치에 소음 이벤트를 발생시킨다(NPC 청각 테스트용). ")
		TEXT("사용법: MOU.Voice.FakeNoise [반경] [음량] [태그=Voice.Proximity] ")
		TEXT("(생략하면 현재 발화 모드의 실제 값을 쓴다)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				APawn* Pawn = PC ? PC->GetPawn() : nullptr;

				if (!Pawn)
				{
					UE_LOG(LogMOUVoice, Warning,
						TEXT("소음을 낼 폰이 없다. 게임에 스폰된 뒤에 사용할 것."));
					return;
				}

				// 기본값을 하드코딩하지 않고 실제 발화 모드 값에서 가져온다.
				// 그래야 NPC 팀원이 이 명령으로 맞춰둔 반응이 V8 의 진짜 음성에도
				// 그대로 유효하다(숫자가 다르면 다시 튜닝해야 한다).
				const UVoiceSubsystem* Voice = FindVoiceSubsystem(World);
				const EVoiceMode Mode = Voice ? Voice->GetVoiceMode() : EVoiceMode::Normal;

				const float MaxRange = Args.IsValidIndex(0)
					? FCString::Atof(*Args[0]) : MOUVoice::GetNoiseRange(Mode);
				const float Loudness = Args.IsValidIndex(1)
					? FCString::Atof(*Args[1]) : MOUVoice::GetLoudnessScale(Mode);
				const FName Tag = Args.IsValidIndex(2) ? FName(*Args[2]) : FName(TEXT("Voice.Proximity"));

				const FVector NoiseLocation = Pawn->GetActorLocation();

				// 소음의 책임자(Instigator)는 "소리가 난 위치에 있는 대상" 이다.
				// NPC 는 이 값으로 누구를 쫓을지 판단한다.
				UAISense_Hearing::ReportNoiseEvent(
					World, NoiseLocation, Loudness, Pawn, MaxRange, Tag);

				UE_LOG(LogMOUVoice, Log,
					TEXT("[가짜 소음] 위치=%s 반경=%.0f 음량=%.2f 태그=%s"),
					*NoiseLocation.ToCompactString(), MaxRange, Loudness, *Tag.ToString());
			}));
}
