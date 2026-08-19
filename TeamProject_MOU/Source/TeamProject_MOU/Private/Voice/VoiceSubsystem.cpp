// MOU 음성 - 음성 시스템 진입점 구현 + 콘솔 명령.
// 대응하는 설계 문서: VOICE_INTEGRATION.md 7절, 11절, 14절(마일스톤)
//
// [스레드]
//   이 파일의 코드는 전부 게임 스레드다.
//   마이크 캡처도 게임 스레드다(VoiceCaptureSource.h 상단 주석에 이유가 있다).
//   유일한 다른 스레드는 오디오 렌더 스레드이고 VoiceSynthComponent.cpp 가 담당한다.

#include "Voice/VoiceSubsystem.h"

#include "Voice/VoiceCaptureSource.h"
#include "Voice/VoiceCodec.h"
#include "Voice/VoiceComponent.h"
#include "Voice/VoicePlaybackComponent.h"
#include "Voice/VoiceRouter.h"
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

namespace
{
	/**
	 * 이 프로세스에서 마이크를 잡고 있는 서브시스템.
	 *
	 * ★ PIE 다중 창 대비책이다(VOICE_INTEGRATION.md 6절).
	 *
	 *   PIE 로 창 두 개를 띄우면 한 프로세스 안에 로컬 플레이어가 둘 생기고,
	 *   서브시스템도 둘이 된다. 둘 다 **같은 물리 마이크를 열려고 해서** 장치
	 *   경합이 난다. 나쁜 것은 실패 방식이다 - 둘째 창이 조용히 실패하거나,
	 *   더 나쁘게는 첫 창의 캡처를 망가뜨려서 **"아까는 됐는데 왜 지금 안 되지"**
	 *   가 된다.
	 *
	 *   그래서 먼저 온 쪽만 마이크를 잡는다. 나머지 창은 **재생만** 한다 -
	 *   V3 의 검증 방법(PIE 2창에서 거리에 따라 들리고 안 들리는지)에는
	 *   말하는 쪽이 하나면 충분하다.
	 *
	 *   진짜 양방향 통화 테스트는 프로세스를 두 개 띄워서 한다.
	 *
	 * 약참조인 이유: 이 포인터를 역참조할 일은 없지만, 서브시스템이 예기치 않게
	 * 사라졌을 때 자리가 영영 안 풀리는 것을 막는다.
	 */
	TWeakObjectPtr<UVoiceSubsystem> GVoiceCaptureOwner;
}

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
	else if (GVoiceCaptureOwner.IsValid())
	{
		// 이 프로세스에서 이미 다른 창이 마이크를 잡았다(위 GVoiceCaptureOwner 주석).
		// 재생은 정상 동작하므로 여기서 끝내도 된다 - 남의 목소리는 들린다.
		UE_LOG(LogMOUVoice, Log,
			TEXT("이 창은 마이크를 열지 않는다(같은 프로세스의 다른 창이 이미 사용 중). ")
			TEXT("듣기만 한다. 양방향 테스트는 프로세스를 두 개 띄울 것."));
	}
	else
	{
		GVoiceCaptureOwner = this;

		// 마이크 열기. 실패해도 게임은 정상 진행된다(재생만 동작).
		CaptureSource = MakeUnique<FVoiceCaptureSource>();
		CaptureSource->Start();

		// 코덱은 마이크와 독립적으로 만든다.
		//
		// ★ 마이크가 없어도 디코더는 필요하다. V3 에서 마이크 없는 팀원도
		//   남의 목소리는 들어야 하기 때문이다(15절: "마이크가 없거나 권한이
		//   없으면 전체 시스템이 죽지 않고 재생만 동작해야 한다").
		//   지금부터 그 구조로 만들어 두면 V3 에서 손댈 것이 없다.
		Encoder = MakeUnique<FMOUVoiceEncoder>();
		Encoder->Initialize();

		Decoder = MakeUnique<FMOUVoiceDecoder>();
		Decoder->Initialize();
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

	// 코덱은 캡처 뒤에 정리한다. 캡처가 살아있는 동안 인코더가 사라지면
	// 남은 프레임을 처리하다 죽은 인코더를 만질 수 있다.
	Encoder.Reset();
	Decoder.Reset();

	// 마이크 자리를 놓아준다. 안 놓으면 PIE 를 껐다 켰을 때 **아무도 마이크를
	// 못 잡는다** - 죽은 서브시스템이 자리를 차지한 채로 남기 때문이다.
	// (약참조라 자동으로 풀리긴 하지만, 여기서 명시적으로 놓는 편이 읽기 쉽다)
	if (GVoiceCaptureOwner.Get() == this)
	{
		GVoiceCaptureOwner.Reset();
	}

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

		// 무음 구간은 여기서 끝낸다. 인코딩도 하지 않는다.
		//
		// VAD 로 무음을 걸러내는 것은 대역폭 절감이기도 하지만, 더 중요하게는
		// **"지금 소리를 내고 있는가" 가 곧 게임 규칙**이기 때문이다(7-1절).
		// V8 의 NPC 소음 이벤트도 같은 판정을 입력으로 쓴다. 그래서 걸러내는
		// 지점이 여기 한 곳이어야 전송·소음·재생이 서로 어긋나지 않는다.
		if (!Frame.bIsSpeaking)
		{
			continue;
		}

		// --- 1. 인코딩 ------------------------------------------------------
		//
		// ★ 루프백 여부와 무관하게 인코딩한다.
		//   V3 에서 이 자리가 "서버로 전송" 이 되기 때문이다. 지금 루프백 분기
		//   안쪽에 넣어두면 V3 에서 다시 끄집어내야 한다.
		//   부수적으로, 루프백을 끈 채로도 MOU.Voice.Stat 의 압축률을 볼 수 있다.
		bool bEncoded = false;
		if (bCodecEnabled && Encoder.IsValid() && Encoder->IsReady())
		{
			bEncoded = Encoder->Encode(Frame.Samples.GetData(), Frame.Samples.Num(), EncodedScratch);
		}

		// --- 2. 서버로 전송 (V3) ---------------------------------------------
		//
		// 여기부터 남이 듣는다. 아래 루프백은 이제 **디버그 전용**이다.
		//
		// ★ 코덱이 꺼져 있으면 아무것도 못 보낸다. 네트워크로 나가는 형식이
		//   Opus 바이트라서 원본 PCM 을 실을 자리가 없기 때문이다.
		//   MOU.Voice.Codec 0 은 그래서 "루프백으로만 들어보는" 모드가 된다.
		if (bEncoded)
		{
			if (UVoiceComponent* Voice = GetVoiceComponent())
			{
				Voice->SendVoiceFrame(EncodedScratch, Frame.Loudness, VoiceMode, EVoiceRoute::Proximity);
				++FramesTransmitted;
			}
		}

		if (!bLoopbackEnabled || !PlaybackComponent)
		{
			// 루프백이 꺼져 있으면 여기서 끝. 남에게는 위에서 이미 보냈다.
			continue;
		}

		// --- 3. 로컬 루프백 (디버그 전용) ------------------------------------
		//
		// 내 목소리를 내가 듣는 경로다. 서버는 발신자 본인에게 프레임을 보내지
		// 않으므로(에코 방지), 이 경로가 없으면 자기 소리를 확인할 방법이 없다.
		//
		// 코덱이 꺼져 있으면 원본 PCM 이 그대로 간다. 켜고 끄며 비교하는 것이
		// V2 의 검증 방법이다(MOU.Voice.Codec).
		const int16* PlaybackSamples = Frame.Samples.GetData();
		int32        PlaybackCount   = Frame.Samples.Num();

		if (bEncoded)
		{
			const bool bDecoded = Decoder.IsValid()
				&& Decoder->Decode(EncodedScratch.GetData(), EncodedScratch.Num(), DecodedScratch);

			if (!bDecoded)
			{
				// ★ 실패했을 때 원본 PCM 으로 대신 재생하지 않는다.
				//   그렇게 하면 코덱이 완전히 고장나 있어도 소리는 멀쩡히 나서
				//   V3 에 가서야 문제를 발견하게 된다. 지금은 조용한 편이 낫다 -
				//   실패 횟수는 MOU.Voice.Stat 에 남는다.
				continue;
			}

			PlaybackSamples = DecodedScratch.GetData();
			PlaybackCount   = DecodedScratch.Num();
		}

		// --- 4. 재생 --------------------------------------------------------
		//
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

		PlaybackComponent->PushSamples(PlaybackSamples, PlaybackCount);
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

	// ★ 발화가 끝나는 순간 디코더 상태를 초기화한다.
	//
	// Opus 디코더는 직전 프레임을 참고해 다음 프레임을 복원한다. 발화 사이에
	// 몇 초의 공백이 있어도 디코더는 그것을 모르므로, 새 발화의 첫 프레임을
	// **공백 전의 소리를 참고해서** 푼다. 그러면 말 첫머리가 짧게 지직거린다.
	//
	// 여기서 잡는 이유: 이 전환(true -> false)은 위의 hangover 로직까지 다 돌고
	// 나야 확정된다. 루프 안에서 프레임 단위로 보면 hangover 로 아직 살아있는
	// 구간을 발화 종료로 오판한다.
	if (bWasSpeaking && !bIsSpeaking && Decoder.IsValid())
	{
		Decoder->Reset();
	}
	bWasSpeaking = bIsSpeaking;

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

APlayerController* UVoiceSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;

	// ★ 인자 없는 ULocalPlayer::GetPlayerController() 는 존재하지 않는다.
	//   (그건 FLocalPlayerContext 의 것이다) UPlayer 의 월드 인자 버전을 쓴다.
	return (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
}

UVoiceComponent* UVoiceSubsystem::GetVoiceComponent()
{
	// 캐시가 살아있으면 그대로 쓴다. 컨트롤러가 갈리면 약참조가 풀려 아래로 떨어진다.
	if (UVoiceComponent* Cached = CachedVoiceComponent.Get())
	{
		return Cached;
	}

	APlayerController* PC = GetOwningPlayerController();
	UVoiceComponent* Found = UVoiceComponent::Find(PC);

	if (!Found)
	{
		// ★ 여기 걸리면 원인은 거의 항상 하나다: 쓰고 있는 PlayerController 클래스가
		//   ATeamProject_MOUPlayerController 를 상속하지 않는다.
		//   증상이 "내 목소리가 아무에게도 안 들린다" 뿐이라 알아채기 어려워서,
		//   PC 가 있는데 컴포넌트만 없는 경우에만 한 번 크게 남긴다.
		//   (PC 자체가 아직 없는 것은 레벨 로드 중 정상 상황이라 조용히 넘어간다)
		static bool bWarnedMissingComponent = false;
		if (PC && !bWarnedMissingComponent)
		{
			bWarnedMissingComponent = true;
			UE_LOG(LogMOUVoice, Error,
				TEXT("★ PlayerController(%s)에 UVoiceComponent 가 없다. 음성이 서버로 나가지 않는다. ")
				TEXT("게임모드의 PlayerController 클래스가 ATeamProject_MOUPlayerController 를 ")
				TEXT("상속하는지 확인할 것."),
				*GetNameSafe(PC->GetClass()));
		}
		return nullptr;
	}

	CachedVoiceComponent = Found;
	return Found;
}

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

void UVoiceSubsystem::SetCodecEnabled(bool bEnabled)
{
	if (bCodecEnabled == bEnabled)
	{
		return;
	}

	bCodecEnabled = bEnabled;

	// 경로가 바뀌는 순간이므로 양쪽 다 정리한다.
	// 코덱을 켜고 끄는 사이에 남은 소리가 섞이면 A/B 비교의 의미가 없다.
	if (PlaybackComponent)
	{
		PlaybackComponent->RequestFlush();
	}
	if (Decoder.IsValid())
	{
		Decoder->Reset();
	}

	if (bEnabled && Encoder.IsValid() && !Encoder->IsReady())
	{
		// 시작할 때 실패했을 수 있다(마이크 권한과 무관하게 모듈 문제 등).
		// 여기서 한 번 더 시도해 본다 - 콘솔로 켜는 시점엔 상황이 달라졌을 수 있다.
		Encoder->Initialize();
	}

	UE_LOG(LogMOUVoice, Log, TEXT("Opus 코덱 %s. %s"),
		bEnabled ? TEXT("ON") : TEXT("OFF"),
		bEnabled ? TEXT("압축을 통과한 소리가 들린다.")
		         : TEXT("원본 PCM 이 그대로 들린다(비교용)."));
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

		// 음소거는 Tick 의 발화 종료 감지를 우회한다(위쪽에서 early return 한다).
		// 그래서 여기서 직접 처리해야 음소거를 풀었을 때 첫마디가 지직거리지 않는다.
		if (Decoder.IsValid())
		{
			Decoder->Reset();
		}
		bWasSpeaking = false;
	}

	UE_LOG(LogMOUVoice, Log, TEXT("마이크 %s."), bMuted ? TEXT("음소거") : TEXT("음소거 해제"));
}

bool UVoiceSubsystem::IsCaptureReady() const
{
	return CaptureSource.IsValid() && CaptureSource->IsReady();
}

bool UVoiceSubsystem::IsCodecReady() const
{
	return Encoder.IsValid() && Encoder->IsReady()
		&& Decoder.IsValid() && Decoder->IsReady();
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

	// --- 코덱 통계 ----------------------------------------------------------
	//
	// 평균 프레임 크기가 12절의 대역폭 계산을 실측으로 검증하는 값이다.
	// 설계는 프레임당 ~60바이트(= 초당 50프레임 x 60 = 3KB/s)를 전제로 했다.
	// 여기 숫자가 그와 크게 다르면 12절의 대역폭 표를 다시 계산해야 한다.
	FString CodecStats;
	if (Encoder.IsValid() && Encoder->GetFrameCount() > 0)
	{
		const float AvgBytes = Encoder->GetAverageFrameBytes();

		// 초당 프레임 수는 고정(1000/FrameMs = 50)이므로 초당 바이트를 바로 낸다.
		const float BytesPerSec = AvgBytes * (1000.f / MOUVoice::FrameMs);

		CodecStats = FString::Printf(
			TEXT(" | 코덱=%s 인코딩=%d프레임 평균%.1f바이트(최대%d) 압축률%.1f:1 %.1fKB/s 디코딩=%d실패=%d"),
			bCodecEnabled ? TEXT("ON") : TEXT("OFF"),
			Encoder->GetFrameCount(),
			AvgBytes,
			Encoder->GetMaxFrameBytes(),
			AvgBytes > 0.f ? MOUVoice::BytesPerFrame / AvgBytes : 0.f,
			BytesPerSec / 1024.f,
			Decoder.IsValid() ? Decoder->GetFrameCount() : 0,
			Decoder.IsValid() ? Decoder->GetFailureCount() : 0);
	}
	else
	{
		CodecStats = FString::Printf(TEXT(" | 코덱=%s (아직 인코딩한 프레임 없음)"),
			bCodecEnabled ? TEXT("ON") : TEXT("OFF"));
	}

	// --- V3 네트워크 통계 ---------------------------------------------------
	//
	// 세 줄로 나눠 보여주는 이유: "안 들린다" 의 원인이 송신/서버/수신 중
	// 어디인지가 이 숫자들의 조합으로 바로 갈린다.
	//   전송=0        -> 마이크나 코덱 문제 (또는 VoiceComponent 가 없다)
	//   전송>0 라우팅=0 -> 서버까지 안 갔다 (RPC 문제)
	//   라우팅>0 전달=0 -> 서버가 수신자를 못 찾았다 (거리, 폰 없음)
	//   전달>0 재생=0   -> 받는 쪽 문제 (디코딩, 발신자 폰 못 찾음)
	FString NetStats = FString::Printf(TEXT(" | 전송=%d"), FramesTransmitted);

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;

	// ★ 라우터 통계는 **서버일 때만** 보여준다.
	//
	//   월드 서브시스템이라 클라이언트에도 객체는 생긴다. 그런데 클라의 라우터는
	//   한 번도 안 불리므로 항상 0 이다. 그걸 그대로 찍으면 "라우팅=0" 을 보고
	//   **서버가 일을 안 한다고 오진하게 된다** - 실제로는 내 화면에 남의 서버
	//   상태가 보일 리 없는 것뿐인데. 아예 안 보여주는 편이 덜 헷갈린다.
	if (World && World->GetNetMode() != NM_Client)
	{
		if (const UVoiceRouter* Router = UVoiceRouter::Get(World))
		{
			NetStats += FString::Printf(TEXT(" | [서버] %s"), *Router->GetStatsString());
		}
	}

	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const UVoicePlaybackComponent* Playback = PC->FindComponentByClass<UVoicePlaybackComponent>())
		{
			NetStats += FString::Printf(TEXT(" | [수신] %s"), *Playback->GetStatsString());
		}
	}

	return FString::Printf(
		TEXT("마이크=%s 음소거=%s 루프백=%s 발화=%s 모드=%s(들림%.0f/NPC%.0f) 감도=%.4f 음량=%.4f ")
		TEXT("| 수신프레임=%d 버림=%d ")
		TEXT("| 재생버퍼=%d샘플(%.0fms) 언더런=%d 오버플로=%d%s%s"),
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
		Overflow,
		*CodecStats,
		*NetStats);
}

// ---------------------------------------------------------------------------
// 콘솔 명령
//
// 사용 예:
//   MOU.Voice.Loopback 1          내 목소리를 내 헤드폰으로 (V1 검증)
//   MOU.Voice.Codec 0             Opus 우회 - 원본과 압축을 A/B 비교 (V2 검증)
//   MOU.Voice.Stat                통계 출력 (압축률과 프레임 크기 포함)
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

	/**
	 * V2 검증의 핵심 도구.
	 *
	 * 루프백을 켠 채로 이 명령을 0/1 로 왕복하며 같은 말을 반복하면
	 * **압축이 음질을 얼마나 깎는지** 직접 비교할 수 있다.
	 * "전화 수준이면 합격" 이라는 기준은 이렇게만 판정할 수 있다 - 숫자로는 안 된다.
	 */
	FAutoConsoleCommandWithWorldAndArgs GVoiceCodecCommand(
		TEXT("MOU.Voice.Codec"),
		TEXT("Opus 코덱을 켜고 끈다(끄면 원본 PCM 이 그대로 재생된다. 음질 비교용). ")
		TEXT("인자 없이 부르면 토글. 사용법: MOU.Voice.Codec [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UVoiceSubsystem* Voice = FindVoiceSubsystem(World))
				{
					const bool bEnable = Args.IsValidIndex(0)
						? (FCString::Atoi(*Args[0]) != 0)
						: !Voice->IsCodecEnabled();

					Voice->SetCodecEnabled(bEnable);
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

				// 5단계: 코덱. 마이크와 별개로 실패할 수 있으므로 따로 찍는다.
				// "마이크는 잡히는데 소리가 안 난다" 의 원인이 여기일 수 있다.
				UE_LOG(LogMOUVoice, Log, TEXT("5) Opus 코덱         : %s (%s)"),
					Voice->IsCodecEnabled() ? TEXT("사용") : TEXT("우회 중"),
					Voice->IsCodecReady() ? TEXT("인코더/디코더 준비됨") : TEXT("X  <- 생성 실패"));

				if (Voice->IsCodecEnabled() && !Voice->IsCodecReady())
				{
					UE_LOG(LogMOUVoice, Log,
						TEXT("   -> 코덱 없이 들어보려면 MOU.Voice.Codec 0 으로 우회할 수 있다."));
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
