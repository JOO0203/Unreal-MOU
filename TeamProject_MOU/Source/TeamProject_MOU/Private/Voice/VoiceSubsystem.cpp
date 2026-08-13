// MOU 음성 - 음성 시스템 진입점 구현 + 콘솔 명령.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7절, 11절, 14절(마일스톤)
//
// [스레드]
//   이 파일의 코드는 전부 게임 스레드다.
//   워커 스레드 코드는 VoiceCaptureRunnable.cpp, 오디오 렌더 스레드 코드는
//   VoiceSynthComponent.cpp 에 있다. 셋을 섞지 말 것.

#include "Voice/VoiceSubsystem.h"

#include "Voice/VoiceCaptureRunnable.h"
#include "Voice/VoiceSynthComponent.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/RunnableThread.h"
#include "Perception/AISense_Hearing.h"

// ---------------------------------------------------------------------------
// 서브시스템 수명
// ---------------------------------------------------------------------------

void UVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 캡처 스레드는 여기서 바로 띄운다.
	//
	// 마이크가 없거나 [Voice] bEnabled 가 꺼져 있어도 스레드는 뜨고, 캡처만
	// 비활성 상태가 된다(IsCaptureReady() == false). 이렇게 해야 마이크 없는
	// 팀원의 게임이 죽지 않는다.
	CaptureRunnable = new FVoiceCaptureRunnable();
	CaptureThread = FRunnableThread::Create(
		CaptureRunnable, TEXT("MOUVoiceCapture"), 0, TPri_AboveNormal);

	if (!CaptureThread)
	{
		// 스레드 생성 자체가 실패한 경우. 러너블을 직접 지운다
		// (스레드가 없으므로 ShutdownCapture 의 Kill 대기가 의미 없다).
		UE_LOG(LogMOUVoice, Error, TEXT("음성 캡처 스레드를 만들지 못했다."));
		delete CaptureRunnable;
		CaptureRunnable = nullptr;
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UVoiceSubsystem::Tick));

	UE_LOG(LogMOUVoice, Log,
		TEXT("음성 서브시스템 초기화 완료. 루프백을 켜려면 MOU.Voice.Loopback 1"));
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

	ShutdownCapture();

	Super::Deinitialize();
}

void UVoiceSubsystem::ShutdownCapture()
{
	// ★ 이 순서를 지키지 않으면 PIE 를 껐다 켤 때 에디터가 통째로 죽는다.
	//   기존 채팅의 UChatSubsystem::ShutdownClient 와 같은 이유다.
	//   1) 종료 요청  2) 스레드가 Run() 을 빠져나올 때까지 대기  3) 그 다음에 해제
	if (CaptureRunnable)
	{
		CaptureRunnable->Stop();
	}

	if (CaptureThread)
	{
		CaptureThread->Kill(/*bShouldWait=*/true); // true 필수
		delete CaptureThread;
		CaptureThread = nullptr;
	}

	if (CaptureRunnable)
	{
		delete CaptureRunnable;
		CaptureRunnable = nullptr;
	}
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
	if (!CaptureRunnable)
	{
		return true;
	}

	bool bReceivedAnyFrame = false;

	FMOUVoiceFrame Frame;
	while (CaptureRunnable->DequeueFrame(Frame))
	{
		++FramesReceived;
		bReceivedAnyFrame = true;
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

	return true; // 계속 틱
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

bool UVoiceSubsystem::IsCaptureReady() const
{
	return CaptureRunnable && CaptureRunnable->IsCaptureReady();
}

float UVoiceSubsystem::GetCurrentLoudness() const
{
	return CaptureRunnable ? CaptureRunnable->GetCurrentLoudness() : 0.f;
}

void UVoiceSubsystem::SetMicSensitivity(float InThreshold)
{
	if (CaptureRunnable)
	{
		CaptureRunnable->SetVadThreshold(InThreshold);
		UE_LOG(LogMOUVoice, Log, TEXT("마이크 감도(VAD 임계값) = %.4f"), InThreshold);
	}
}

float UVoiceSubsystem::GetMicSensitivity() const
{
	return CaptureRunnable ? CaptureRunnable->GetVadThreshold() : MOUVoice::DefaultVadThreshold;
}

FString UVoiceSubsystem::GetStatsString() const
{
	const int32 Buffered = PlaybackComponent ? PlaybackComponent->GetBufferedSampleCount() : 0;
	const int32 Underrun = PlaybackComponent ? PlaybackComponent->GetUnderrunCount() : 0;
	const int32 Overflow = PlaybackComponent ? PlaybackComponent->GetOverflowCount() : 0;

	return FString::Printf(
		TEXT("마이크=%s 루프백=%s 발화=%s 감도=%.4f 음량=%.4f | 수신프레임=%d 버림=%d ")
		TEXT("| 재생버퍼=%d샘플(%.0fms) 언더런=%d 오버플로=%d"),
		IsCaptureReady() ? TEXT("준비됨") : TEXT("없음"),
		bLoopbackEnabled ? TEXT("ON") : TEXT("OFF"),
		bIsSpeaking ? TEXT("O") : TEXT("X"),
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
		TEXT("사용법: MOU.Voice.FakeNoise [반경=1500] [음량=1.0] [태그=Voice.Proximity]"),
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

				const float MaxRange = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 1500.f;
				const float Loudness = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 1.f;
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
